/*
 * Copyright 2013 Real Logic Ltd.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#ifndef _IR_COLLECTION_H_
#define _IR_COLLECTION_H_

#if defined(WIN32)
#include <sys/types.h>
#include <sys/stat.h>
#include <io.h>
#define fileno _fileno
#define read _read
#define stat _stat64
#else
#include <sys/types.h>
#include <sys/uio.h>
#include <unistd.h>
#include <sys/stat.h>
#endif /* WIN32 */

#include <map>
#include <iostream>

#include "otf_api/Ir.h"
#include "uk_co_real_logic_sbe_ir_generated/TokenCodec.hpp"
#include "uk_co_real_logic_sbe_ir_generated/FrameCodec.hpp"

using namespace sbe::on_the_fly;
using namespace uk_co_real_logic_sbe_ir_generated;

namespace sbe {
namespace on_the_fly {

/**
 * \brief Collection that holds the Ir for several message template IDs as well as a header for
 * dispatching them. Equivalent to the Java IntermediateRepresentation class.
 */
class IrCollection
{
public:
    /// Construct a collection
    IrCollection() : header_(NULL)
    {
    }

    virtual ~IrCollection()
    {
        delete[] buffer_;
        for (std::multimap<int, Ir *>::iterator it = map_.begin(); it != map_.end(); ++it)
        {
//            std::cout << (*it).first << " => " << (*it).second << '\n';
            Ir *ir = (*it).second;
            delete ir;
            (*it).second = NULL;
        }
        if (header_ != NULL)
        {
            delete header_;
        }
    }

    /**
     * \brief Load a collection from a serialized IR file generated by the Java SbeTool.
     *
     * \param filename to load from
     * \return 0 for success or -1 for failure
     */
    int loadFromFile(const char *filename)
    {
        if ((length_ = IrCollection::getFileSize(filename)) < 0)
        {
            return -1;
        }
        std::cout << "IR Filename " << filename << " length " << length_ << std::endl;
        if (length_ == 0)
        {
            return -1;
        }
        buffer_ = new char[length_];

        if (IrCollection::readFileIntoBuffer(buffer_, filename, length_) < 0)
        {
            return -1;
        }

        if (processIr() < 0)
        {
            return -1;
        }
        return 0;
    }

    /*
     * TODO: provide version of loadFromFile that takes { list of IDs, callback to call when ID encountered }.
     * TODO: this is to provide means of knowing exact offset for a given ID.
     * TODO: if ID in repeating group or VAR_DATA, then return Ir::VARIABLE_SIZE
     */

    /**
     * \brief Return the Ir for the header used for message dispatch
     *
     * \return Ir for the header
     */
    Ir &header(void) const
    {
        return *header_;
    }

    /**
     * \brief Return the Ir for the message with the given id
     *
     * \param id of the message
     * \param version of the message
     * \return Ir for the message or NULL if not found
     */
    const Ir *message(int id, int version) const
    {
        std::pair<std::multimap<int, Ir *>::const_iterator, std::multimap<int, Ir *>::const_iterator> ret;

        ret = map_.equal_range(id);
        for (std::multimap<int, Ir *>::const_iterator it = ret.first; it != ret.second; it++)
        {
            if (it->second->templateId() == id && it->second->templateVersion() == version)
            {
                return it->second;
            }
        }
        return NULL;
    }

protected:
    // OS specifics
    static int getFileSize(const char *filename)
    {
        struct stat fileStat;

        if (::stat(filename, &fileStat) != 0)
        {
            return -1;
        }

        return fileStat.st_size;
    }

    static int readFileIntoBuffer(char *buffer, const char *filename, int length)
    {
        FILE *fptr = ::fopen(filename, "r");
        int remaining = length;

        if (fptr == NULL)
        {
            return -1;
        }

        int fd = fileno(fptr);
        while (remaining > 0)
        {
            int sz = ::read(fd, buffer + (length - remaining), (4098 < remaining) ? 4098 : remaining);
            remaining -= sz;
            if (sz < 0)
            {
                break;
            }
        }
        fclose(fptr);
        return (remaining == 0) ? 0 : -1;
    }

    int processIr(void)
    {
        FrameCodec frame;
        int offset = 0, tmpLen = 0;
        char tmp[256];

        frame.wrapForDecode(buffer_, offset, frame.blockLength(), frame.templateVersion());
        tmpLen = frame.getPackageVal(tmp, sizeof(tmp));

        ::std::cout << "Reading IR package=\"" << std::string(tmp, tmpLen) << "\"" << ::std::endl;

        offset += frame.size();

        headerLength_ = readHeader(offset);

        offset += headerLength_;

        while (offset < length_)
        {
            offset += readMessage(offset);
        }

        return 0;
    }

    int readHeader(int offset)
    {
        TokenCodec token;
        int size = 0;

        while (offset + size < length_)
        {
            char tmp[256], name[256];
            int nameLen = 0;

            token.wrapForDecode(buffer_, offset + size, token.blockLength(), token.templateVersion());

            nameLen = token.getName(name, sizeof(name));
            token.getConstVal(tmp, sizeof(tmp));
            token.getMinVal(tmp, sizeof(tmp));
            token.getMaxVal(tmp, sizeof(tmp));
            token.getNullVal(tmp, sizeof(tmp));
            token.getCharacterEncoding(tmp, sizeof(tmp));
            token.getEpoch(tmp, sizeof(tmp));
            token.getTimeUnit(tmp, sizeof(tmp));
            token.getSemanticType(tmp, sizeof(tmp));

            size += token.size();

            if (token.signal() == SignalCodec::BEGIN_COMPOSITE)
            {
                std::cout << " Header name=\"" << std::string(name, nameLen) << "\"";
            }

            if (token.signal() == SignalCodec::END_COMPOSITE)
            {
                break;
            }
        }

        std::cout << " length " << size << std::endl;

        header_ = new Ir(buffer_ + offset, size);

        return size;
    }

    int readMessage(int offset)
    {
        TokenCodec token;
        int size = 0;

        while (offset + size < length_)
        {
            char tmp[256], name[256];
            int nameLen = 0;

            token.wrapForDecode(buffer_, offset + size, token.blockLength(), token.templateVersion());

            nameLen = token.getName(name, sizeof(name));
            token.getConstVal(tmp, sizeof(tmp));
            token.getMinVal(tmp, sizeof(tmp));
            token.getMaxVal(tmp, sizeof(tmp));
            token.getNullVal(tmp, sizeof(tmp));
            token.getCharacterEncoding(tmp, sizeof(tmp));
            token.getEpoch(tmp, sizeof(tmp));
            token.getTimeUnit(tmp, sizeof(tmp));
            token.getSemanticType(tmp, sizeof(tmp));

            size += token.size();

            if (token.signal() == SignalCodec::BEGIN_MESSAGE)
            {
                std::cout << " Message name=\"" << std::string(name, nameLen) << "\"";
                std::cout << " id=\"" << token.schemaId() << "\"";
                std::cout << " version=\"" << token.tokenVersion() << "\"";
            }

            if (token.signal() == SignalCodec::END_MESSAGE)
            {
                break;
            }
        }

        std::cout << " length " << size << std::endl;

        // save buffer_ + offset as start of message and size as length

        map_.insert(std::pair<int, Ir *>(token.schemaId(), new Ir(buffer_ + offset, size, token.schemaId(), token.tokenVersion())));

        // map_[token.schemaID()] = new Ir(buffer_ + offset, size);

        return size;
    }

private:
    std::multimap<int, Ir *> map_;

    char *buffer_;
    int length_;

    Ir *header_;
    int headerLength_;
};

} // namespace on_the_fly

} // namespace sbe

#endif /* _IR_COLLECTION_H_ */
