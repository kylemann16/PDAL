/******************************************************************************
* Copyright (c) 2018, Kyle Mann (kyle@hobu.co)
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
*     * Neither the name of Hobu, Inc. or Flaxen Geo Consulting nor the
*       names of its contributors may be used to endorse or promote
*       products derived from this software without specific prior
*       written permission.
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
#include "stacReader.hpp"

#include <pdal/util/ProgramArgs.hpp>
#include <pdal/Kernel.hpp>
#include <nlohmann/json.hpp>
#include <schema-validator/json-schema.hpp>
#include <pdal/util/Bounds.hpp>

namespace pdal
{

static PluginInfo const stacinfo
{
    "readers.stac",
    "STAC Reader",
    "http://pdal.io/stages/readers.stac.html"
};

CREATE_SHARED_STAGE(StacReader, stacinfo)

std::string StacReader::getName() const { return stacinfo.name; }

//TODO make local stac catalog, with different reader types

void StacReader::addArgs(ProgramArgs& args)
{
    m_args.reset(new StacReader::Args());

    args.add("asset_name", "Asset to use for data consumption", m_args->assetName, "data");
    args.add("date_ranges", "Date ranges to include in your search. "
        "Eg. dates'[{\"min\":\"min1\",\"max\":\"max1\"},...]'", m_args->dates);
    args.add("bounds", "Bounding box to select stac items by. This will "
        "propogate down through all readers being used.", m_args->bounds);
    args.add("ids", "List of ID regexes to select STAC items based on.", m_args->ids);
    args.add("schema_validate", "Use JSON schema to validate your STAC objects.", m_args->schemaValidate, false);
    args.add("properties", "Map of STAC property names to regular expression "
        "values. ie. {\"pc:type\": \"(lidar|sonar)\"}. Selected items will "
        "match all properties.", m_args->properties);
    args.add("reader_args", "Map of reader arguments to their values to pass through.",
        m_args->readerArgs);
    args.add("dry_run", "Dry run, will log ids to be run", m_args->dryRun);
}

void StacReader::handleReaderArgs()
{
    for (NL::json& readerPipeline: m_args->readerArgs)
    {
        if (!readerPipeline.contains("type"))
            throw pdal_error("No \"type\" key found in supplied reader arguments.");

        std::string driver = readerPipeline["type"].get<std::string>();
        if (m_readerArgs.contains(driver))
            throw pdal_error("Multiple instances of the same driver in supplie reader arguments.");
        m_readerArgs[driver] = { };

        for (auto& arg: readerPipeline.items())
        {
            if (arg.key() == "type")
                continue;
            m_readerArgs[driver][arg.key()] = arg.value();
        }
    }
    std::cout << m_readerArgs.dump() << std::endl;
}

void StacReader::initializeArgs()
{
    // should be a string vector, ["foo", "bar", "USGS_LPC_AK_Fairbanks_2009"]
    if (!m_args->ids.empty())
    {

        log()->get(LogLevel::Debug) << "Selecting Ids: " << std::endl;
        for (auto& id: m_args->ids)
            log()->get(LogLevel::Debug) << "    " << id << std::endl;
    }

    // A 2D array of dates, [[minDate1, maxDate1], [minDate2, maxDate2], ...]
    if (!m_args->dates.empty())
    {
        //TODO validate supplied dates?
        log()->get(LogLevel::Debug) << "Dates selected: " << m_args->dates  << std::endl;
    }

    // A nlohmann JSON object with a key value pair that maps to properties in
    // a STAC item. { "pc:encoding": "ept" }
    if (!m_args->properties.empty())
    {
        if (!m_args->properties.is_object())
            throw pdal_error("Properties argument must be a valid JSON object.");
        log()->get(LogLevel::Debug) << "Property Pruning: " <<
            m_args->properties.dump() << std::endl;
    }

    // An array of SrsBounds objects, [([xmin, xmax], [ymin, ymax], [zmin, zmax]), ...]
    if (!m_args->bounds.empty())
    {
        if (!m_args->bounds.valid())
            throw pdal_error("Supplied bounds are not valid.");
        log()->get(LogLevel::Debug) << "Bounds: " << m_args->bounds << std::endl;
    }

    // array of pipeline-like reader definitions
    //{ "type": "readers.ept" , "resolution": 100, "bounds": "([x,x],[y,y])"}
    if (!m_args->readerArgs.empty())
    {
        for (auto& opts: m_args->readerArgs)
            if (!opts.is_object())
                throw pdal_error("Reader Args must be a valid JSON object");

        // if (!m_args->readerArgs.is_object())
        //     throw pdal_error("Reader Args must be a valid JSON object");
        handleReaderArgs();
    }

    if (!m_args->assetName.empty())
        log()->get(LogLevel::Debug) << "STAC Reader will look for assets in "
            "asset name '" << m_args->assetName << "'." << std::endl;

    if (m_args->dryRun)
        log()->get(LogLevel::Debug) << "Dry Run flag is set." << std::endl;

    if (m_args->schemaValidate)
        log()->get(LogLevel::Debug) <<
            "JSON Schema validation flag is set." << std::endl;

}

void StacReader::initialize()
{
    initializeArgs();

    m_arbiter.reset(new arbiter::Arbiter());
    std::string stacStr = m_arbiter->get(m_filename);
    NL::json stacJson = NL::json::parse(stacStr);

    if (!stacJson.contains("type"))
        throw pdal_error("Invalid STAC object provided.");

    std::string stacType = stacJson["type"];
    if (stacType == "Feature")
        initializeItem(stacJson);
    else if (stacType == "Catalog")
        initializeCatalog(stacJson);
    else
        throw pdal_error("Could not initialize STAC object of type " + stacType);
}

void schemaFetch(const nlohmann::json_uri &json_uri, nlohmann::json &json)
{
    std::unique_ptr<arbiter::Arbiter> fetcher;
    fetcher.reset(new arbiter::Arbiter());
    std::string jsonStr = fetcher->get(json_uri.url());
    json = nlohmann::json::parse(jsonStr);
}

void StacReader::schemaValidate(NL::json stacJson)
{
    std::function<void(const nlohmann::json_uri &, nlohmann::json &)> fetch = schemaFetch;
    nlohmann::json_schema::json_validator val(
        fetch,
        [](const std::string &, const std::string &) {}
    );
    if (!stacJson.contains("type"))
        throw pdal_error("Invalid STAC json");
    std::string type = stacJson["type"].get<std::string>();
    std::string schemaUrl;

    if (type == "Feature")
    {
        schemaUrl = "https://schemas.stacspec.org/v1.0.0/item-spec/json-schema/item.json";
        for (auto& extSchemaUrl: stacJson["stac_extensions"])
        {
            log()->get(LogLevel::Debug) << "Processing extension " << extSchemaUrl << std::endl;
            std::string schemaStr = m_arbiter->get(extSchemaUrl);
            NL::json schemaJson = NL::json::parse(schemaStr);
            val.set_root_schema(schemaJson);
            // try {
                val.validate(stacJson);
            // } catch(const nlohmann::json_schema::error_handler& e) {
            //     throw pdal_error("Failed to validate STAC JSON against extension " +
            //         extSchemaUrl.dump() + " with error " + e.message);
            // }
        }
    }
    else if (type == "Catalog")
        schemaUrl = "https://schemas.stacspec.org/v1.0.0/catalog-spec/json-schema/catalog.json";
    else
        throw pdal_error("Invalid STAC type for PDAL consumption");

    std::string schemaStr = m_arbiter->get(schemaUrl);
    NL::json schemaJson = NL::json::parse(schemaStr);
    val.set_root_schema(schemaJson);
    val.validate(stacJson);
}

void StacReader::initializeItem(NL::json stacJson)
{
    if (prune(stacJson))
        return;

    if (m_args->schemaValidate)
        schemaValidate(stacJson);

    if (!stacJson["assets"].contains(m_args->assetName))
        throw pdal_error("asset_name("+m_args->assetName+") doesn't match STAC object.");

    std::string dataUrl = stacJson["assets"][m_args->assetName]["href"].get<std::string>();
    std::string driver = m_factory.inferReaderDriver(dataUrl);

    log()->get(LogLevel::Debug) << "Using driver " << driver <<
        " for file " << dataUrl << std::endl;

    Stage *reader = PluginManager<Stage>::createObject(driver);

    if (!reader)
        throwError("Unable to create reader for file '" + dataUrl + "'.");

    Options readerOptions;
    // add reader options defined in reader args to their respective readers
    if (m_readerArgs.contains(driver)) {
        NL::json args = m_readerArgs[driver].get<NL::json>();
        for (auto& arg : args.items()) {
            readerOptions.add(arg.key(), arg.value());
        }
    }

    readerOptions.add("filename", dataUrl);
    reader->setOptions(readerOptions);

    // m_merge.setInput(*reader);

    if (m_readerList.size() > 0)
        reader->setInput(*m_readerList.back());

    if (reader)
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_readerList.push_back(std::unique_ptr<Stage>(reader));
    }
}

void StacReader::initializeCatalog(NL::json stacJson)
{
    if (m_args->schemaValidate)
        schemaValidate(stacJson);
    auto itemLinks = stacJson["links"];
    for (auto link: itemLinks)
    {
        std::string linkType = link["rel"];
        if (linkType != "item")
            continue;
        std::string itemUrl = link["href"];
        //Create json from itemUrl
        NL::json itemJson = NL::json::parse(m_arbiter->get(itemUrl));
        initializeItem(itemJson);
    }
}

// returns true if property matches
bool matchProperty(std::string key, NL::json val, NL::json properties, NL::detail::value_t type)
{
    switch (type)
    {
        case NL::detail::value_t::string:
        {
            std::regex desired(val);
            std::string value = properties[key].get<std::string>();
            if (!std::regex_match(value, desired))
                return false;
            break;
        }
        case NL::detail::value_t::number_unsigned:
        {
            uint value = properties[key].get<uint>();
            uint desired = val.get<uint>();
            if (value != desired)
                return false;
            break;
        }
        case NL::detail::value_t::number_integer:
        {
            int value = properties[key].get<int>();
            int desired = val.get<int>();
            if (value != desired)
                return false;
            break;
        }
        case NL::detail::value_t::number_float:
        {
            int value = properties[key].get<int>();
            int desired = val.get<int>();
            if (value != desired)
                return false;
            break;
        }
        case NL::detail::value_t::boolean:
        {
            bool value = properties[key].get<bool>();
            bool desired = val.get<bool>();
            if (value != desired)
                return false;
            break;
        }
        default:
        {
            throw pdal_error("Data type of " + key + " is not supported for pruning.");
        }
    }
    return true;
}

bool StacReader::prune(NL::json stacJson)
{
    // Returns true if item should be removed, false if it should stay

    // ID
    // If STAC ID matches *any* ID in supplied list, it will not be pruned.
    std::string itemId = stacJson["id"];
    bool idFlag = true;
    if (!m_args->ids.empty())
    {
        for (auto& id: m_args->ids)
        {
            std::regex id_regex(id);
            if (std::regex_match(itemId, id_regex))
            {
                idFlag = false;
            }
        }
    }
    else
        idFlag = false;

    if (idFlag)
        return true;

    // DateTime
    // If STAC datetime fits in *any* of the supplied ranges, it will not be pruned
    std::string stacDate = stacJson["properties"]["datetime"];
    bool dateFlag = true;
    if (m_args->dates.empty())
        dateFlag = false;
    for (auto& range: m_args->dates)
    {
        //If the extracted item date fits into any of the dates provided by
        //the user, then do not prune this item based on dates.
        if (
            stacDate >= range[0].get<std::string>() &&
            stacDate <= range[1].get<std::string>()
        )
            dateFlag = false;
    }
    if (dateFlag)
        return true;

    // Properties
    // If STAC properties match *all* the supplied properties, it will not be pruned
    NL::json properties = stacJson["properties"];
    if (!m_args->properties.empty())
    {
        for (auto &it: m_args->properties.items())
        {
            if (!properties.contains(it.key()))
            {
                log()->get(LogLevel::Warning) << "STAC Item does not contain "
                    "property " << it.key() << ". Continuing." << std::endl;
                continue;
            }

            NL::detail::value_t type = properties[it.key()].type();
            NL::detail::value_t argType = it.value().type();
            //Array of possibilities are Or'd together
            if (argType == NL::detail::value_t::array)
            {
                bool arrFlag = true;
                for (auto& val: it.value())
                    if (matchProperty(it.key(), val, properties, type))
                        arrFlag = false;
                if (arrFlag)
                    return true;
            }
            else
                if (!matchProperty(it.key(), it.value(), properties, type))
                    return true;
        }
    }

    // bbox
    // If STAC bbox matches *any* of the supplied bounds, it will not be pruned
    if (!m_args->bounds.empty())
    {
        NL::json bboxJson = stacJson["bbox"].get<NL::json>();
        if (bboxJson.size() == 4)
        {
            double minx = bboxJson[0];
            double miny = bboxJson[1];
            double maxx = bboxJson[2];
            double maxy = bboxJson[3];
            BOX2D bbox = BOX2D(minx, miny, maxx, maxy);
            if (!m_args->bounds.to2d().overlaps(bbox))
                return true;
        }
        else if (bboxJson.size() == 6)
        {
            double minx = bboxJson[0];
            double miny = bboxJson[1];
            double minz = bboxJson[2];
            double maxx = bboxJson[3];
            double maxy = bboxJson[4];
            double maxz = bboxJson[5];
            BOX3D bbox = BOX3D(minx, miny, minz, maxx, maxy, maxz);
            if (!m_args->bounds.to3d().overlaps(bbox))
                return true;
        }
    }

    log()->get(LogLevel::Debug) << "Including: " << itemId << std::endl;
    m_idList.push_back(itemId);
    return false;
}

QuickInfo StacReader::inspect()
{
    initialize();
    QuickInfo qi;
    // QuickInfo qi = m_readerList.back()->preview();

    for (auto& reader: m_readerList)
    {
        QuickInfo readerQi = reader->preview();
        qi.m_bounds.grow(readerQi.m_bounds);
        qi.m_pointCount += readerQi.m_pointCount;
        qi.m_metadata["id"] = NL::json::array();
        for (auto& id: m_idList)
            qi.m_metadata["id"].push_back(id);

        for (auto& readerDim: readerQi.m_dimNames)
        {
            bool exists = false;
            for (auto& dim: qi.m_dimNames)
                if (dim == readerDim)
                    exists = true;
            if (!exists)
                qi.m_dimNames.push_back(readerDim);
        }
    }

    qi.m_valid = true;
    return qi;
}

void StacReader::prepared(PointTableRef table)
{
    m_readerList.back()->prepare(table);
    // for (auto& reader: m_readerList)
    //     reader->prepare(table);
    // if (!m_args->dryRun)
    //     m_merge.prepare(table);
    // else
    // {
    //     for (std::string& id : m_idList)
    //     {
    //         log()->get(LogLevel::Info) << id << std::endl;
    //     }
    // }
}

void StacReader::ready(PointTableRef table)
{
    m_pvSet = m_readerList.back()->execute(table);

    // for (auto& reader: m_readerList)
    // if (!m_args->dryRun)
    //     m_pvSet = m_merge.execute(table);
}

void StacReader::done(PointTableRef)
{
    m_stream.reset();
}

PointViewSet StacReader::run(PointViewPtr view)
{
    return m_pvSet;
}

} //namespace pdal