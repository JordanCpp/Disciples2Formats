/*
 * This file is part of the random scenario generator for Disciples 2.
 * (https://github.com/VladimirMakeev/D2RSG)
 * Copyright (C) 2023 Vladimir Makeev.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <FfReader.hpp>
#include <stdexcept>
#include <assert.h>
#include <limits>
#include <string.h>

#define FFSIGNATURE(a, b, c, d)                                                                    \
    ((static_cast<uint32_t>(d) << 24) | (static_cast<uint32_t>(c) << 16)                 \
     | (static_cast<uint32_t>(b) << 8) | static_cast<uint32_t>(a))

static uint32_t mqdbFileSignature = FFSIGNATURE('M', 'Q', 'D', 'B');
static uint32_t mqdbFileVersion = 9;
static uint32_t mqrcSignature = FFSIGNATURE('M', 'Q', 'R', 'C');

static const char indexOptRecordName[] = "-INDEX.OPT";
static const char imagesOptRecordName[] = "-IMAGES.OPT";

static uint32_t paletteSize = 11 + 1024;

/**
 * Reads 4-byte value from buffer at specified offset.
 * Adjusts offset after reading.
 */
static inline uint32_t readUint32(const char* contents, size_t& byteOffset)
{
    const uint32_t value = *reinterpret_cast<const uint32_t*>(&contents[byteOffset]);
    byteOffset += sizeof(uint32_t);

    return value;
}

/** Reads 4-byte value from file. */
static inline uint32_t readUint32(std::ifstream& file)
{
    uint32_t value = 0;
    file.read(reinterpret_cast<char*>(&value), sizeof(value));

    return value;
}

FfReader::FfReader(const std::string& ffFilePath, bool readImageData)
    : ffFilePath(ffFilePath)
{
    assert(sizeof(MqdbHeader) == 24 && "Size of MqdbHeader structure must be exactly 24 bytes");
    assert(sizeof(TocRecord) == 16 && "Size of TocRecord structure must be exactly 16 bytes");
    assert(sizeof(MqrcHeader) == 28 && "Size of MqrcHeader structure must be exactly 28 bytes");

    std::ifstream file(ffFilePath.c_str(), std::ios_base::binary);
    if (!file) {
        throw std::runtime_error("Could not open MQDB file");
    }

    checkFileHeader(file);
    readTableOfContents(file);
    readNameList(file);
    readIndex(file);

    if (readImageData) {
        readImages(file);
    }
}

FfReader::~FfReader()
{
}

const TocRecord* FfReader::findTocRecord(RecordId recordId) const
{
    std::map<RecordId, TocRecord>::const_iterator it = tableOfContents.find(recordId);

    return it != tableOfContents.end() ? &it->second : NULL;

    return NULL;
}

const TocRecord* FfReader::findTocRecord(SpecialId recordId) const
{
    return findTocRecord(static_cast<RecordId>(recordId));
}

const TocRecord* FfReader::findTocRecord(const char* recordName) const
{
    return findTocRecord(std::string(recordName));
}

const TocRecord* FfReader::findTocRecord(const std::string& recordName) const
{
    std::map<std::string, RecordId>::const_iterator it = recordNames.find(recordName);

    return it != recordNames.end() ? findTocRecord(it->second) : NULL;

    return NULL;
}

bool FfReader::getRecordData(const std::string& recordName, std::vector<char>& data)
{
    const TocRecord* record = findTocRecord(recordName);
    if (!record) {
        return false;
    }

    return getRecordData(*record, data);
}

bool FfReader::getRecordData(RecordId recordId, std::vector<char>& data)
{
    const TocRecord* record = findTocRecord(recordId);
    if (!record) {
        return false;
    }

    return getRecordData(*record, data);
}

std::vector<std::string> FfReader::getNames() const
{
    std::vector<std::string> namesArray(recordNames.size());

    size_t i = 0;
    for (std::map<std::string, RecordId>::const_iterator pair = recordNames.begin(); pair != recordNames.end(); pair++) {
        namesArray[i++] = pair->first;
    }

    return namesArray;
}

void FfReader::checkFileHeader(std::ifstream& file)
{
    MqdbHeader header;
    file.read(reinterpret_cast<char*>(&header), sizeof(header));

    if (header.signature != mqdbFileSignature) {
        throw std::runtime_error("Not a MQDB file");
    }

    if (header.version != mqdbFileVersion) {
        throw std::runtime_error("Wrong MQDB file version");
    }
}

void FfReader::readTableOfContents(std::ifstream& file)
{
    const uint32_t tocOffset = readUint32(file);

    file.seekg(tocOffset);

    const uint32_t entriesTotal = readUint32(file);

    for (uint32_t i = 0; i < entriesTotal; ++i) {
        TocRecord record;
        file.read(reinterpret_cast<char*>(&record), sizeof(record));

        if (tableOfContents.find(record.recordId) != tableOfContents.end()) {
            throw std::runtime_error("MQDB ToC contains records with non-unique ids");
        }

        tableOfContents[record.recordId] = record;
    }
}

void FfReader::readNameList(std::ifstream& file)
{
    const TocRecord* namesList = findTocRecord(NameList);
    if (!namesList) {
        // MQDB file must contain name list record
        throw std::runtime_error("Could not find MQDB names list ToC record");
    }

    // Start reading names list contents, skip record header
    file.seekg(namesList->offset + sizeof(MqrcHeader));

    const uint32_t namesTotal = readUint32(file);
    
    for (uint32_t i = 0; i < namesTotal; ++i) {
        char name[256];
        file.read(name, sizeof(name));
        name[255] = '\0';

        const RecordId recordId = readUint32(file);

        // Find record by its id, check if it is deleted or not
        const TocRecord* tocRecord = findTocRecord(recordId);
        if (!tocRecord) {
            // This should never happen
            continue;
        }

        const std::streampos readPosition = file.tellg();

        file.seekg(tocRecord->offset, std::ios_base::beg);

        
        MqrcHeader recordHeader;
        file.read(reinterpret_cast<char*>(&recordHeader), sizeof(recordHeader));
        if (recordHeader.signature != mqrcSignature) {
            // Mqrc record header has wrong signature.
            // Either current algorithm is wrong
            // or .ff file contains garbage or unknown structures.
            // This is unrecoverable.
            throw std::runtime_error("Read wrong MQRC signature while processing names list");
        }

        // Restore position previously read from
        file.seekg(readPosition, std::ios_base::beg);

        if (!recordHeader.used) {
            // Record is not used, do not store it in names list
            continue;
        }
        
        const std::string nameString = name;

        if (recordNames.find(nameString) != recordNames.end()) {
            // Do not store duplicates, for now.
            // Duplicates shouldn't exist in MQDB files, especially there shouldn't be
            // several MQRC records with the same name, since game loads MQRC contents
            // using names, not their ids.
            // For example Motlin's mod 1.4.1 has Interf.ff with INDEXMAP#63.PNG.PNG duplicates.
            // id 6700 and 6701
            // These duplicates most likely were created by D2ResExplorer
            // since it does not delete entries
            continue;
        }
        
        recordNames[nameString] = recordId;
    }
}

void FfReader::readIndex(std::ifstream& file)
{
    const TocRecord* record = findTocRecord(indexOptRecordName);
    if (!record) {
        // No index record present, skip
        return;
    }

    file.seekg(record->offset + sizeof(MqrcHeader));

    std::vector<char> contents(record->size, '\0');
    file.read(&contents[0], record->size);

    const char* contentsPtr = &contents[0];

    size_t byteOffset = 0;
    const uint32_t total = readUint32(contentsPtr, byteOffset);

    for (uint32_t i = 0; i < total; ++i) {
        const RecordId id = readUint32(contentsPtr, byteOffset);

        const char* name = &contentsPtr[byteOffset];
        // +1 for null terminator
        const size_t nameLength = strlen(name) + 1;

        byteOffset += nameLength;

        const uint32_t offset = readUint32(contentsPtr, byteOffset);
        const uint32_t size = readUint32(contentsPtr, byteOffset);

        const std::string entryName(name);

        if (id != std::numeric_limits<RecordId>::max()) {
            // Entry has valid id, this is an image entry
            ImageIndices images = indexData.images;

            images.ids.push_back(id);
            images.names.push_back(entryName);
            images.packedInfo.push_back(PackedImageInfo(offset, size));
        } else {
            // Entries with invalid ids are used for animation frames
            AnimationIndices animations = indexData.animations;

            animations.names.push_back(entryName);
            animations.packedInfo.push_back(PackedImageInfo(offset, size));
        }
    }
}

void FfReader::readImages(std::ifstream& file)
{
    const TocRecord* record = findTocRecord(imagesOptRecordName);
    if (!record) {
        // No images record present, skip
        return;
    }

    const uint32_t off = record->offset + sizeof(MqrcHeader);
    file.seekg(off, std::ios_base::beg);

    const uint32_t recordSize = record->size;

    std::vector<char> contents(recordSize, '\0');
    file.read(&contents[0], recordSize);

    const char* contentsPtr = &contents[0];
    size_t byteOffset = 0;

    while (byteOffset < recordSize) {
        const uint32_t offset = static_cast<RelativeOffset>(byteOffset);

        PackedImage packedImage;

        packedImage.palette.assign(&contentsPtr[byteOffset],
                                   &contentsPtr[byteOffset + paletteSize]);
        byteOffset += paletteSize;

        const uint32_t framesTotal = readUint32(contentsPtr, byteOffset);

        packedImage.frames.reserve(framesTotal);

        for (uint32_t i = 0; i < framesTotal; ++i) {
            const char* frameName = &contentsPtr[byteOffset];
            // +1 for null terminator
            const size_t nameLength = strlen(frameName) + 1;

            byteOffset += nameLength;

            const uint32_t partsTotal = readUint32(contentsPtr, byteOffset);
            const uint32_t frameWidth = readUint32(contentsPtr, byteOffset);
            const uint32_t frameHeight = readUint32(contentsPtr, byteOffset);

            ImageFrame frame(frameName, frameWidth, frameHeight);
            frame.parts.reserve(partsTotal);

            for (uint32_t j = 0; j < partsTotal; ++j) {
                const uint32_t sourceX = readUint32(contentsPtr, byteOffset);
                const uint32_t sourceY = readUint32(contentsPtr, byteOffset);

                const uint32_t targetX = readUint32(contentsPtr, byteOffset);
                const uint32_t targetY = readUint32(contentsPtr, byteOffset);

                const uint32_t partWidth = readUint32(contentsPtr, byteOffset);
                const uint32_t partHeight = readUint32(contentsPtr, byteOffset);

                ImagePart part;
                part.sourceX = sourceX;
                part.sourceY = sourceY;
                part.targetX = targetX;
                part.targetY = targetY;
                part.width   = partWidth;
                part.height  = partHeight;

                frame.parts.push_back(part);
            }

            packedImage.frames.push_back(frame);
        }

        packedImages[offset] = packedImage;
    }
}

bool FfReader::getRecordData(const TocRecord& record, std::vector<char>& data)
{
    std::ifstream file(ffFilePath.c_str(), std::ios_base::binary);
    if (!file) {
        return false;
    }

    file.seekg(record.offset + sizeof(MqrcHeader));

    data.resize(record.size);
    file.read(&data[0], record.size);
    return true;
}