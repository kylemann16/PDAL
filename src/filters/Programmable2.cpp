/******************************************************************************
* Copyright (c) 2011, Michael P. Gerlek (mpg@flaxen.com)
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

#include <pdal/filters/Programmable2.hpp>

#include <pdal/PointBuffer.hpp>

namespace pdal { namespace filters {


Programmable2::Programmable2(Stage& prevStage, const Options& options)
    : pdal::Filter(prevStage, options)
    , m_program("")
{
    return;
}

void Programmable2::initialize()
{
    Filter::initialize();
    
    m_program = getOptions().getValueOrDefault<std::string>("program", "") ;
    log()->get(logDEBUG)  << "program " << m_program << std::endl;

    assert(m_program != "");

    return;
}


const Options Programmable2::getDefaultOptions() const
{
    Options options;
    Option program("program", "");
    options.add(program);
    return options;
}


void Programmable2::processBuffer(PointBuffer& data, pdal::plang::PythonMethod& python) const
{
    python.beginChunk(data);

    const Schema& schema = data.getSchema();
    boost::uint32_t numSrcPoints = data.getNumPoints();

    Dimension const& dimX = schema.getDimension("X");
    Dimension const& dimY = schema.getDimension("Y");
    Dimension const& dimZ = schema.getDimension("Z");

    for (boost::uint32_t srcIndex=0; srcIndex<numSrcPoints; srcIndex++)
    {
        const double x = data.getField<double>(dimX, srcIndex);
        const double y = data.getField<double>(dimY, srcIndex);
        const double z = data.getField<double>(dimZ, srcIndex);

        //parser.setVariable<double>("X", x);
        //parser.setVariable<double>("Y", y);
        //parser.setVariable<double>("Z", z);

        bool ok = python.execute();
        assert(ok);

        //const double xx = parser.getVariable<double>("X");
        //const double yy = parser.getVariable<double>("Y");
        //const double zz = parser.getVariable<double>("Z");

        //data.setField<double>(dimX, srcIndex, xx);
        //data.setField<double>(dimY, srcIndex, yy);
        //data.setField<double>(dimZ, srcIndex, zz);

        //data.setField<double>(dimX, srcIndex, x);
        //data.setField<double>(dimY, srcIndex, y);
        //data.setField<double>(dimZ, srcIndex, z);

        //data.setNumPoints(srcIndex+1);
    }

    python.endChunk(data);

    return;
}


pdal::StageSequentialIterator* Programmable2::createSequentialIterator(PointBuffer& buffer) const
{
    return new pdal::filters::iterators::sequential::Programmable2(*this, buffer);
}


//---------------------------------------------------------------------------


namespace iterators { namespace sequential {


Programmable2::Programmable2(const pdal::filters::Programmable2& filter, PointBuffer& buffer)
    : pdal::FilterSequentialIterator(filter, buffer)
    , m_programmableFilter(filter)
    , m_pythonEnv(NULL)
    , m_pythonMethod(NULL)
{
    return;
}


Programmable2::~Programmable2()
{
    //delete m_parser;
}


void Programmable2::createParser()
{
    const std::string program = m_programmableFilter.getProgram();

    m_pythonEnv = new pdal::plang::PythonEnvironment();
    m_pythonEnv->startup();

    m_pythonMethod = new pdal::plang::PythonMethod(*m_pythonEnv, program);

    return;
}


boost::uint32_t Programmable2::readBufferImpl(PointBuffer& data)
{
    if (!m_pythonMethod)
    {
        createParser();
    }

    const boost::uint32_t numRead = getPrevIterator().read(data);

    m_programmableFilter.processBuffer(data, *m_pythonMethod);

    return numRead;
}


boost::uint64_t Programmable2::skipImpl(boost::uint64_t count)
{
    getPrevIterator().skip(count);
    return count;
}


bool Programmable2::atEndImpl() const
{
    return getPrevIterator().atEnd();
}

} } // iterators::sequential

} } // pdal::filters
