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

void StacReader::addArgs(ProgramArgs& args)
{
    m_args.reset(new StacReader::Args());


    args.add("asset_name", "Asset to use for data consumption", m_args->assetName, "data");
    args.add("min_date", "Minimum date to accept STAC items. Inclusive.", m_args->minDate);
    args.add("max_date", "Maximum date to accept STAC items. Inclusive.", m_args->maxDate);
    args.add("id", "Regular expression of IDs to select", m_args->id);

}

void StacReader::initialize(PointTableRef table)
{

    if (!m_args->minDate.empty())
        log()->get(LogLevel::Debug) << "Minimum Date: " << m_args->minDate << std::endl;

    if (!m_args->maxDate.empty())
        log()->get(LogLevel::Debug) << "Maximum Date: " << m_args->maxDate << std::endl;

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

void StacReader::initializeItem(NL::json stacJson)
{
    if (prune(stacJson))
        return;

    std::string dataUrl = stacJson["assets"][m_args->assetName]["href"];
    std::string driver = m_factory.inferReaderDriver(dataUrl);
    log()->get(LogLevel::Debug) << "Using driver " << driver <<
        " for file " << dataUrl << std::endl;

    Stage *reader = m_factory.createStage(driver);
    Stage *merge = m_factory.createStage("filters.merge");

    if (!reader)
        throwError("Unable to create reader for file '" + dataUrl + "'.");

    Options readerOptions;
    readerOptions.add("filename", dataUrl);
    reader->setOptions(readerOptions);

    m_merge.setInput(*reader);
}

void StacReader::initializeCatalog(NL::json stacJson)
{
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

bool StacReader::prune(NL::json stacJson)
{
    // Returns true if item should be removed, false if it should stay
    std::string itemId = stacJson["id"];
    if (!m_args->id.empty())
    {
        std::regex id_regex(m_args->id);
        if (!std::regex_match(itemId, id_regex))
        {
            log()->get(LogLevel::Debug) << "Id " << itemId <<
                " does not match " << m_args->id << std::endl;
            return true;
        }
    }

    std::string stacDate = stacJson["properties"]["datetime"];
    if (stacDate <= m_args->minDate && !m_args->minDate.empty())
    {
        log()->get(LogLevel::Debug) << "Id " << itemId <<
            " has date (" << stacDate <<
            ") less than the minimum date. Excluding." << std::endl;
        return true;
    }

    if (stacDate >= m_args->maxDate && !m_args->maxDate.empty())
    {
        log()->get(LogLevel::Debug) << "Id " << itemId <<
            " has date (" << stacDate <<
            ") greater than the maximum date. Excluding." << std::endl;
        return true;
    }

    log()->get(LogLevel::Debug) << "Id " << itemId <<
        " will be included." << std::endl;
    return false;

}

void StacReader::prepared(PointTableRef table)
{
    m_merge.prepare(table);
}

void StacReader::ready(PointTableRef table)
{
    m_pvSet = m_merge.execute(table);
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