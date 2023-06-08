/******************************************************************************
 * Copyright (c) 2022, Kyle Mann (kyle@hobu.co)
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following
 * conditions are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided
 *       with the distribution.
 *     * Neither the name of the Martin Isenburg or Iowa Department
 *       of Natural Resources nor the names of its contributors may be
 *       used to endorse or promote products derived from this software
 *       without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 ****************************************************************************/

#include "Catalog.hpp"
#include "Utils.hpp"

#include <nlohmann/json.hpp>
#include <schema-validator/json-schema.hpp>

namespace pdal
{

namespace stac
{
    Catalog::Catalog(const NL::json& json,
            const std::string& catPath,
            const connector::Connector& connector,
            ThreadPool& pool,
            const LogPtr& logPtr) :
        m_json(json), m_path(catPath), m_connector(connector),
        m_pool(pool), m_log(logPtr)
    {}

    Catalog::~Catalog()
    {}

    void Catalog::init(std::vector<std::string> assetNames, NL::json rawReaderArgs)
    {
        if (!m_json.contains("id"))
        {
            std::stringstream msg;
            msg << "Invalid catalog. It is missing key 'id'.";
            throw pdal_error(msg.str());
        }

        std::string catalogId = m_json.at("id").get<std::string>();

        auto itemLinks = m_json.at("links");

        m_log->get(LogLevel::Debug) << "Filtering..." << std::endl;

        for (const auto& link: itemLinks)
        {

            if (!link.count("href") || !link.count("rel"))
                throw pdal::pdal_error("item does not contain 'href' or 'rel'");

            const std::string linkType = link.at("rel").get<std::string>();
            const std::string linkPath = link.at("href").get<std::string>();
            const std::string absLinkPath = handleRelativePath(m_path, linkPath);

            m_pool.add([this, linkType, absLinkPath, assetNames, rawReaderArgs]()
            {
                try
                {
                    if (linkType == "item")
                    {
                        NL::json itemJson = m_connector.getJson(absLinkPath);
                        Item item(itemJson, absLinkPath, m_connector);
                        std::lock_guard<std::mutex> lock(m_mutex);
                        m_itemList.push_back(item);
                        // initializeItem(itemJson, absLinkPath);
                    }
                    else if (linkType == "catalog")
                    {
                        NL::json catalogJson = m_connector.getJson(absLinkPath);
                        // initializeCatalog(catalogJson, absLinkPath);
                        Catalog catalog(catalogJson, absLinkPath, m_connector,
                            m_pool, m_log);
                        catalog.init(assetNames, rawReaderArgs);
                        std::vector<Item> items = catalog.items();
                        std::lock_guard<std::mutex> lock(m_mutex);
                        for (auto& i: items)
                        {
                            m_itemList.push_back(i);
                        }
                        // m_itemList.insert(m_itemList.end(), items.size(), items);
                    }
                }
                catch (std::exception& e)
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    std::pair<std::string, std::string> p {absLinkPath, e.what()};
                    m_errors.push_back(p);
                }
                catch (...)
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    m_errors.push_back({absLinkPath, "Unknown error"});
                }
            });
            m_pool.await();
        }

        if (m_errors.size())
        {
            for (auto& p: m_errors)
            {
                m_log->get(LogLevel::Error) << "Failure fetching '" << p.first << "' with error '"
                    << p.second << "'";
            }
        }

    }

    std::vector<Item> Catalog::items()
    {
        return m_itemList;
    }


    void Catalog::validate()
    {
        std::function<void(const nlohmann::json_uri&, nlohmann::json&)> fetch = schemaFetch;
        nlohmann::json_schema::json_validator val(
            fetch,
            [](const std::string &, const std::string &) {}
        );
        if (!m_json.contains("type"))
            throw pdal_error("Invalid STAC json");
        std::string type = m_json.at("type").get<std::string>();
        std::string schemaUrl;

        schemaUrl = m_schemaUrl;

        NL::json schemaJson = m_connector.getJson(schemaUrl);
        val.set_root_schema(schemaJson);
        val.validate(m_json);
    }

    bool Catalog::filter(Filters filters) {
        // if (!m_args->catalog_ids.empty() && !isRoot)
        // {
        //     bool pruneFlag = true;
        //     for (auto& id: m_args->catalog_ids)
        //     {
        //         if (catalogId == id)
        //         {
        //             pruneFlag = false;
        //             break;
        //         }
        //     }
        //     if (pruneFlag)
        //         return;
        // }
        return true;
    }


}// stac

}// pdal