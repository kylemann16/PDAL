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

#include "Item.hpp"
#include <pdal/Polygon.hpp>

#include <nlohmann/json.hpp>
#include <schema-validator/json-schema.hpp>

namespace pdal
{

namespace stac
{

using namespace StacUtils;

Item::Item(const NL::json& json,
        const std::string& itemPath,
        const connector::Connector& connector,
        bool validate):
    m_json(json), m_path(itemPath), m_connector(connector),
    m_validate(validate)
{}

Item::~Item()
{}

// Item::Item(const Item& item):
//     m_json(item.m_json), m_path(item.m_path), m_connector(item.m_connector),
//     m_driver(item.m_driver), m_schemaUrls(item.m_schemaUrls),
//     m_readerOptions(item.m_readerOptions)
// {}

bool Item::init(const Filters& filters, NL::json rawReaderArgs,
        SchemaUrls schemaUrls)
{

    if (!filter(filters))
        return false;

    m_schemaUrls = schemaUrls;
    if (m_validate)
        validate();


    NL::json readerArgs = handleReaderArgs(rawReaderArgs);
    m_readerOptions = setReaderOptions(readerArgs, m_driver);
    m_readerOptions.add("filename", m_assetPath);
    return true;
}

std::string Item::id()
{
    return stacId(m_json);
}

const std::string Item::driver()
{
    return m_driver;
}

std::string Item::assetPath()
{
    return m_assetPath;
}

Options Item::options()
{
    return m_readerOptions;
}

NL::json Item::handleReaderArgs(NL::json rawReaderArgs)
{
    if (rawReaderArgs.is_object())
    {
        NL::json array_args = NL::json::array();
        array_args.push_back(rawReaderArgs);
        rawReaderArgs = array_args;
    }
    for (auto& opts: rawReaderArgs)
        if (!opts.is_object())
            throw pdal_error("Reader Args for reader '" + m_driver +
                "' must be a valid JSON object");

    NL::json readerArgs;
    for (const NL::json& readerPipeline: rawReaderArgs)
    {
        const std::string &driver =
            jsonValue<std::string>(readerPipeline, "type");
        if (rawReaderArgs.contains(driver))
            throw pdal_error("Multiple instances of the same driver in"
                " supplied reader arguments.");
        readerArgs[driver] = { };

        for (auto& arg: readerPipeline.items())
        {
            if (arg.key() == "type")
                continue;

            std::string key = arg.key();
            readerArgs[driver][key] = { };
            readerArgs[driver][key] = arg.value();
        }
    }
    return readerArgs;
}

Options Item::setReaderOptions(const NL::json& readerArgs,
    const std::string& driver) const
{
    Options readerOptions;
    if (readerArgs.contains(driver)) {
        const NL::json &args = jsonValue(readerArgs, driver);
        for (auto& arg : args.items())
        {
            std::string key = arg.key();
            NL::json val = arg.value();
            NL::detail::value_t type = val.type();

            // if value is of type string, dump() returns string with
            // escaped string inside and kills pdal program args
            // std::string v;
            if (type == NL::detail::value_t::string)
                readerOptions.add(key, &jsonValue<std::string>(val));
            else
                readerOptions.add(key, arg.value().dump());
            // readerOptions.add(key, v);
        }
    }

    return readerOptions;
}

std::string Item::extractDriverFromItem(const NL::json& asset) const
{
    std::string output;

    std::map<std::string, std::string> contentTypes =
    {
        { "application/vnd.laszip+copc", "readers.copc"}
    };

    const std::string &assetPath = stacValue<std::string>(
        asset, "href", m_json);
    std::string dataUrl = handleRelativePath(m_path, assetPath);

    if (asset.contains("type"))
    {
        const std::string &contentType = stacValue<std::string>(asset, "type", m_json);
        for(const auto& ct: contentTypes)
            if (Utils::iequals(ct.first, contentType))
                return ct.second;
    }

    if (!FileUtils::fileExists(dataUrl))
    {
        // Use this to test if dataUrl is a valid endpoint
        // Try to make a HEAD request and get it from Content-Type
        try
        {
            StringMap headers = m_connector.headRequest(dataUrl);
            if (headers.find("Content-Type") != headers.end())
            {
                const std::string contentType = headers["Content-Type"];
                for(const auto& ct: contentTypes)
                    if (Utils::iequals(ct.first, contentType))
                        return ct.second;
            }
        }
        catch(std::exception& e)
        {
            throw stac_error(m_id, "item", "Failed to HEAD " + dataUrl +
                ". " + e.what());
        }
    }

    // Try to guess from the path
    std::string driver = m_factory.inferReaderDriver(dataUrl);
    if (driver.size())
        return driver;

    return output;
}

void Item::validate()
{

    nlohmann::json_schema::json_validator val(
        [this](const nlohmann::json_uri& json_uri, nlohmann::json& json) {
            json = m_connector.getJson(json_uri.url());
        },
        [](const std::string &, const std::string &) {}
    );

    // Validate against base Item schema first
    NL::json schemaJson = m_connector.getJson(m_schemaUrls.item);
    val.set_root_schema(schemaJson);
    try {
        val.validate(m_json);
    }
    catch (std::exception &e)
    {
        throw stac_error(m_id, "item",
            "STAC schema validation Error in root schema: " +
            m_schemaUrls.item + ". \n\n" + e.what());
    }

    // Validate against stac extensions if present
    if (m_json.contains("stac_extensions"))
    {
        const NL::json &extensions = stacValue(m_json, "stac_extensions");
        for (auto& extSchemaUrl: extensions)
        {
            const std::string &url = stacValue<std::string>(extSchemaUrl, "", m_json);

            try {
                NL::json schemaJson = m_connector.getJson(url);
                val.set_root_schema(schemaJson);
                val.validate(m_json);
            }
            catch (std::exception& e) {
                std::string msg  =
                    "STAC Validation Error in extension: " + url +
                    ". Errors found: \n" + e.what();
                throw stac_error(m_id, "item", msg);

            }
        }
    }
}

void validateForFilter(const NL::json &json)
{
    stacId(json);
    stacValue(json, "assets");
    stacValue(json, "properties");
    stacValue(json, "geometry");
}

bool matchProperty(std::string key, NL::json val, const NL::json &properties,
    NL::detail::value_t type)
{
    switch (type)
    {
        case NL::detail::value_t::string:
        {
            const std::string &desired = jsonValue<std::string>(val);
            const std::string &value = jsonValue<std::string>(properties, key);
            return value == desired;
            break;
        }
        case NL::detail::value_t::number_unsigned:
        {
            const uint64_t &value = jsonValue<uint64_t>(properties, key);
            const uint64_t &desired = jsonValue<uint64_t>(val);
            return value == desired;
            break;
        }
        case NL::detail::value_t::number_integer:
        {
            const int &value = jsonValue<int64_t>(properties, key);
            const int &desired = jsonValue<int64_t>(val);
            return value == desired;
            break;
        }
        case NL::detail::value_t::number_float:
        {
            const double &value = jsonValue<double>(properties, key);
            const double &desired = jsonValue<double>(val);
            return value == desired;
            break;
        }
        case NL::detail::value_t::boolean:
        {
            const bool &value = jsonValue<bool>(properties, key);
            const bool &desired = jsonValue<bool>(val);
            return value == desired;
            break;
        }
        default:
        {
            throw pdal_error("Data type of " + key +
                " is not supported for filtering.");
        }
    }
    return true;
}



bool Item::filter(const Filters& filters)
{
    validateForFilter(m_json);
    m_id = stacId(m_json);

    if (!filterAssets(filters.assetNames))
        return false;

    if (!filterIds(filters.ids))
        return false;

    if (!filterCol(filters.collections))
        return false;

    if (!filterDates(filters.datePairs))
        return false;

    if (!filterProperties(filters.properties))
        return false;

    if (!filterBounds(filters.bounds, filters.srs))
        return false;


    return true;
}

bool Item::filterBounds(BOX3D bounds, SpatialReference srs)
{
    if (bounds.empty())
        return true;

    //Skip bbox altogether and stick with geometry, which will be much
    //more descriptive than bbox
    Polygon stacPolygon;
    const SpatialReference stacSrs("EPSG:4326");
    if (m_json.find("bbox") != m_json.end())
    {
        const auto& edges = stacValue<NL::json::array_t>(m_json, "bbox");
        if (edges.size() == 4)
        {
            const BOX3D stacbox(BOX2D(
                edges[0].get_ref<const double&>(),
                edges[1].get_ref<const double&>(),
                edges[2].get_ref<const double&>(),
                edges[3].get_ref<const double&>()
            ));
            stacPolygon = Polygon(stacbox);
        }
        else if (edges.size() == 6)
        {
            const BOX3D stacbox(
                edges[0].get_ref<const double&>(),
                edges[1].get_ref<const double&>(),
                edges[2].get_ref<const double&>(),
                edges[3].get_ref<const double&>(),
                edges[4].get_ref<const double&>(),
                edges[5].get_ref<const double&>()
            );
            stacPolygon = Polygon(stacbox);
        }

        stacPolygon.setSpatialReference(stacSrs);
    }
    else
    {
        //If stac item has null geometry and bounds have been included
        //for filtering, then the Item will be excluded.
        const NL::json& geometry = stacValue(m_json, "geometry");
        std::string g = geometry.dump();
        // if (geometry.type() == NL::detail::value_t::null)
        //     return false;

        //STAC's base geometries will always be represented in 4326.
        Polygon stacPolygon(g, stacSrs);
        if (!stacPolygon.valid())
            throw stac_error(m_id, "item",
                "Polygon created from STAC 'geometry' key is invalid");
    }

    Polygon userPolygon(bounds);
    if (!srs.empty() && srs != stacSrs)
    {
        userPolygon.setSpatialReference(srs);
        auto status = stacPolygon.transform(srs);
        if (!status)
            throw stac_error(m_id, "item", status.what());
    }
    else
        userPolygon.setSpatialReference("EPSG:4326");

    if (!userPolygon.valid())
        throw pdal_error("User input polygon is invalid, " + bounds.toBox());

    if (!stacPolygon.disjoint(userPolygon))
        return true;

    return false;

}

bool Item::filterProperties(const NL::json& filterProps)
{
    const NL::json &itemProperties = stacValue(m_json, "properties");
    if (!filterProps.empty())
    {
        for (auto &it: filterProps.items())
        {
            std::string key = it.key();
            const NL::json &stacVal = stacValue(itemProperties, key, m_json);
            NL::detail::value_t stacType = stacVal.type();

            NL::json filterVal = it.value();
            NL::detail::value_t filterType = filterVal.type();

            //Array of possibilities are Or'd together
            if (filterType == NL::detail::value_t::array)
            {
                bool arrFlag (true);
                for (auto& val: filterVal)
                    if (matchProperty(key, val, itemProperties, stacType))
                        return true;
            }
            else
                if (matchProperty(key, filterVal, itemProperties, stacType))
                    return true;
        }

        return false;
    }
    return true;
}

bool Item::filterDates(DatePairs dates)
{
    const NL::json &properties = stacValue(m_json, "properties");

    // DateTime
    // If STAC datetime fits in *any* of the supplied ranges,
    // it will be accepted.
    if (!dates.empty())
    {
        if (properties.contains("datetime") &&
            properties.at("datetime").type() != NL::detail::value_t::null)
        {
            const std::string &stacDateStr = stacValue<std::string>(properties,
                "datetime", m_json);

            try
            {
                std::time_t stacTime = getStacTime(stacDateStr);
                for (const auto& range: dates)
                    if (stacTime >= range.first && stacTime <= range.second)
                        return true;
            }
            catch (pdal_error& e)
            {
                throw stac_error(m_id, "item", e.what());
            }

            return false;
        }
        else if (properties.contains("start_datetime") &&
            properties.contains("end_datetime"))
        {
                // Handle if STAC object has start and end datetimes instead of one
                const std::string &endDateStr = stacValue<std::string>(properties,
                    "end_datetime", m_json);
                const std::string &startDateStr = stacValue<std::string>(properties,
                    "end_datetime", m_json);

                std::time_t stacEndTime = getStacTime(endDateStr);
                std::time_t stacStartTime = getStacTime(startDateStr);

                for (const auto& range: dates)
                {
                    // If any of the date ranges overlap with the date range of the STAC
                    // object, do not prune.
                    std::time_t userMinTime = range.first;
                    std::time_t userMaxTime = range.second;

                    if (userMinTime >= stacStartTime && userMinTime <= stacEndTime)
                        return true;
                    else if (userMaxTime >= stacStartTime && userMaxTime <= stacEndTime)
                        return true;
                    else if (userMinTime <= stacStartTime && userMaxTime >= stacEndTime)
                        return true;
                }
                return false;
        }
        else
            throw stac_error(m_id, "item", "Unexpected layout of STAC dates");
    }
    return true;

}

bool Item::filterAssets(std::vector<std::string> assetNames)
{
    const NL::json &assetList = stacValue(m_json, "assets");
    for (auto& name: assetNames)
    {
        if (assetList.contains(name))
        {
            const NL::json &asset = stacValue(assetList, name, m_json);
            m_driver = extractDriverFromItem(asset);
            const std::string &assetPath = stacValue<std::string>(asset, "href", m_json);
            m_assetPath = handleRelativePath(m_path, assetPath);
        }
    }
    if (m_driver.empty())
        return false;
    return true;
}

// If STAC ID matches any ID in supplied list, it will be accepted
bool Item::filterIds(std::vector<RegEx> ids)
{
    if (!ids.empty())
    {
        for (auto& id: ids)
            if (std::regex_match(m_id, id.regex()))
                return true;
        return false;
    }
    return true;
}

bool Item::filterCol(std::vector<RegEx> ids)
{
    if (!ids.empty())
    {
        if (!m_json.contains("collection"))
            return false;

        const std::string &colId = stacValue<std::string>(
            m_json, "collection");
        for (auto& id: ids)
            if (std::regex_match(colId, id.regex()))
                return true;

        return false;
    }
    return true;

}

} //namespace stac

} //namespace pdal
