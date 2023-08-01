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
#include "Collection.hpp"

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
            bool validate) :
        m_json(json), m_path(catPath), m_connector(connector),
        m_pool(pool), m_validate(validate)
    {}

    Catalog::~Catalog()
    {}

    bool Catalog::init(const Filters& filters, NL::json rawReaderArgs,
            SchemaUrls schemaUrls, bool isRoot=false)
    {
        m_root = isRoot;
        if (!filter(filters))
            return false;

        m_schemaUrls = schemaUrls;
        if (m_validate)
            validate();

        NL::json itemLinks = m_utils.stacValue(m_json, "links");

        for (const auto& link: itemLinks)
        {

            const std::string linkType = m_utils.stacValue<std::string>(
                link, "rel", m_json);
            const std::string linkPath = m_utils.stacValue<std::string>(
                link, "href", m_json);

            const std::string absLinkPath = m_utils.handleRelativePath(m_path, linkPath);

            m_pool.add([this, linkType, absLinkPath, filters, rawReaderArgs]()
            {
                try
                {
                    if (linkType == "item")
                    {
                        NL::json itemJson = m_connector.getJson(absLinkPath);
                        Item item(itemJson, absLinkPath, m_connector, m_validate);

                        bool valid = item.init(*filters.itemFilters,
                            rawReaderArgs, m_schemaUrls);
                        if (valid)
                        {
                            std::lock_guard<std::mutex> lock(m_mutex);
                            m_itemList.push_back(item);
                        }
                    }
                    else if (linkType == "catalog")
                    {
                        NL::json catalogJson = m_connector.getJson(absLinkPath);
                        std::unique_ptr<Catalog> catalog(new Catalog(
                            catalogJson, absLinkPath, m_connector, m_pool,
                            m_validate));

                        bool valid = catalog->init(filters, rawReaderArgs,
                            m_schemaUrls);
                        if (valid)
                        {
                            std::lock_guard<std::mutex> lock(m_mutex);
                            m_subCatalogs.push_back(std::move(catalog));
                        }
                    }
                    else if (linkType == "collection")
                    {
                        NL::json collectionJson = m_connector.getJson(absLinkPath);
                        std::unique_ptr<Collection> collection(new Collection(
                            collectionJson, absLinkPath, m_connector, m_pool,
                            m_validate));

                        bool valid = collection->init(filters, rawReaderArgs,
                            m_schemaUrls);
                        if (valid)
                        {
                            std::lock_guard<std::mutex> lock(m_mutex);
                            m_subCatalogs.push_back(std::move(collection));
                        }
                    }
                }
                catch (std::exception& e)
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    StacError p {absLinkPath, e.what()};
                    m_errors.push_back(p);
                }
                catch (...)
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    StacError p {absLinkPath, "Unknown error"};
                    m_errors.push_back(p);
                }
            });
        }


        if (isRoot)
        {
            m_pool.await();
            m_pool.join();
            handleNested();
        }

        return true;
    }

    // Wait for all nested catalogs to finish processing their items so they can
    // be added to the overarching itemlist
    void Catalog::handleNested()
    {
        for (auto& catalog: m_subCatalogs)
        {
            for (const Item& i: catalog->items())
                m_itemList.push_back(i);

            for (StacError& e: catalog->errors())
                m_errors.push_back(e);
        }

    }

    ItemList& Catalog::items()
    {
        return m_itemList;
    }

    ErrorList Catalog::errors()
    {
        return m_errors;
    }

    void Catalog::validate()
    {
        nlohmann::json_schema::json_validator val(
            [this](const nlohmann::json_uri& json_uri, nlohmann::json& json) {
                NL::json tempJson = m_connector.getJson(json_uri.url());

                std::lock_guard<std::mutex> lock(m_mutex);
                json = tempJson;
            },
            [](const std::string &, const std::string &) {}
        );

        NL::json schemaJson = m_connector.getJson(m_schemaUrls.catalog);
        val.set_root_schema(schemaJson);
        try {
            val.validate(m_json);
        }
        catch (std::exception &e)
        {
            throw stac_error(m_id, "catalog",
                "STAC schema validation Error in root schema: " +
                m_schemaUrls.catalog + ". \n\n" + e.what());
        }
    }

    //if catalog matches filter requirements, return true
    bool Catalog::filter(Filters filters) {
        if (filters.ids.empty() || m_root)
            return true;

        m_id = m_utils.stacId(m_json);
        for (auto& i: filters.ids)
            if (std::regex_match(m_id, i.regex()))
                return true;

        return false;
    }


}// stac

}// pdal