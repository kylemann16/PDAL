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
#pragma once

#include <time.h>
#include <ctime>
#include <regex>

#include <pdal/PointView.hpp>
#include <pdal/Reader.hpp>
#include <pdal/util/IStream.hpp>
#include <pdal/SrsBounds.hpp>
#include <pdal/JsonFwd.hpp>
#include <pdal/StageFactory.hpp>
#include <pdal/PipelineManager.hpp>
#include <filters/MergeFilter.hpp>
#include <arbiter/arbiter.hpp>


namespace pdal
{
    class StacReader : public Reader
    {
        public:

            StacReader() : Reader() {};

            std::string getName() const;


        private:
            struct Args
            {
                std::vector<std::string> ids;
                NL::json properties;
                NL::json::array_t readerArgs;
                NL::json::array_t dates;
                SrsBounds bounds;
                std::string assetName;
                bool schemaValidate;
                bool dryRun;
            };


            std::unique_ptr<ILeStream> m_stream;
            std::unique_ptr<Args> m_args;
            std::unique_ptr<arbiter::Arbiter> m_arbiter;
            NL::json m_readerArgs;
            std::vector<std::string> m_idList;
            std::vector<std::unique_ptr<Stage>> m_readerList;
            std::mutex m_mutex;

            StageFactory m_factory;
            MergeFilter m_merge;
            PointViewSet m_pvSet;

            virtual void handleReaderArgs();
            virtual void initialize();
            virtual void initializeItem(NL::json stacJson);
            virtual void initializeCatalog(NL::json stacJson);
            virtual void initializeArgs();
            virtual void schemaValidate(NL::json stacJson);
            virtual bool prune(NL::json stacJson);
            virtual void addArgs(ProgramArgs& args);
            virtual QuickInfo inspect();
            virtual void prepared(PointTableRef table);
            virtual void ready(PointTableRef table);
            virtual void done(PointTableRef table);
            virtual PointViewSet run(PointViewPtr view);
    };
}