#ifndef FfReader_hpp
#define FfReader_hpp

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

#include "pstdint.h"
#include <fstream>
#include <map>
#include <utility>
#include <vector>

/** Header of MQDB (.ff) file. */
struct MqdbHeader
{
    uint32_t signature; /**< File signature, must be 'MQDB'. */
    uint32_t unknown;
    uint32_t version; /**< File version, must be 9. */
    uint32_t unknown2[3];
};

/** Special MQRC records have their own predefined ids. */
enum SpecialId
{
    NameList = 2 /**< Names list MQRC record. */
};

typedef uint32_t RecordId;

/** Table of contents record inside MQDB (.ff) file. */
struct TocRecord
{
    RecordId recordId;           /**< Unique record id. */
    uint32_t size;          /**< Size of contents in this record, in bytes. */
    uint32_t sizeAllocated; /**< Total record size in file, in bytes. */
    uint32_t offset;        /**< Offset from start of the file to associated MQRC record. */
};

/** Header of MQRC record inside MQDB (.ff) file. */
struct MqrcHeader
{
    uint32_t signature; /**< Record signature, must be 'MQRC'. */
    uint32_t unknown;
    RecordId recordId;           /**< Unique record id. */
    uint32_t size;          /**< Size of contents in this record, in bytes. */
    uint32_t sizeAllocated; /**< Total record size in file, in bytes. */
    uint32_t used;          /**< If not zero, indicates that record can be used. */
    uint32_t unknown2;
};



/**
 * Describes part of a packed image.
 * Packed images contain shuffled rectangular areas (parts).
 * Each part is described by ImagePart structure that can be found inside '-IMAGES.OPT'.
 * Areas described as top-left corner and size.
 */
struct ImagePart
{
    uint32_t sourceX; /**< X coordinate of shuffled image part. */
    uint32_t sourceY; /**< Y coordinate of shuffled image part. */
    uint32_t targetX; /**< X coordinate of part in final image. */
    uint32_t targetY; /**< Y coordinate of part in final image. */
    uint32_t width;   /**< Width of image part. */
    uint32_t height;  /**< Height of image part. */
};

/**
 * Describes packed image or an animation frame.
 */
struct ImageFrame
{
    ImageFrame(const char* name, uint32_t width, uint32_t height)
        : name(name)
        , width(width)
        , height(height)
    { }

    std::vector<ImagePart> parts; /**< Parts used for unpacking. */
    std::string name;             /**< Name of this frame. */
    uint32_t width;          /**< Width of unpacked frame. */
    uint32_t height;         /**< Height of unpacked frame. */
};

/**
 * Describes packed image or animation.
 * Simple packed image will contain a single frame that describes its contents,
 * whereas animation would contain multiple frames.
 * All animation frames must have the same width and height to be read correctly by the game.
 */
struct PackedImage
{
    std::vector<char> palette; /**< 11 + 1024 bytes. 11-byte header and 256 4-byte colors. */
    std::vector<ImageFrame> frames;
};

typedef uint32_t RelativeOffset;
typedef uint32_t PackedImageSize;

typedef std::pair<RelativeOffset, /**< Offset from the beginning of '-IMAGES.OPT'
                                                   * or '-ANIMS.OPT' records where PackedImage is
                                                   * stored.
                                                   */
                                  PackedImageSize /**< Total size of PackedImage data, in bytes. */
                                  > PackedImageInfo;

/**
 * Entries of '-INDEX.OPT' describing packed images.
 * Ids, names and packedInfo arrays must have the same number of elements.
 * This way it is possible to get index of image name
 * and access corresponding RecordId or PackedImageInfo with the same index.
 */
struct ImageIndices
{
    std::vector<RecordId> ids;      /**< Ids of MQRC records where raw data is stored. */
    std::vector<std::string> names; /**< Names of images. */
    std::vector<PackedImageInfo> packedInfo;
};

/**
 * Entries of '-INDEX.OPT' describing packed animations.
 * As with ImageIndices, names and packedInfo arrays
 * must have the same number of elements.
 */
struct AnimationIndices
{
    std::vector<std::string> names; /**< Names of animations. */
    std::vector<PackedImageInfo> packedInfo;
};

/** Entries of '-INDEX.OPT' MQRC record. */
struct IndexData
{
    ImageIndices images;
    AnimationIndices animations;
};

class FfReader
{
public:
    FfReader(const std::string& ffFilePath, bool readImageData = true);
    ~FfReader();

    /**
     * Searches for table of contents record by specified id.
     * @returns found record or nullptr.
     */
    const TocRecord* findTocRecord(RecordId recordId) const;

    /** Searches for table of contents record by special id. */
    const TocRecord* findTocRecord(SpecialId recordId) const;

    /** Searches for table of contents record by name. */
    const TocRecord* findTocRecord(const char* recordName) const;
    const TocRecord* findTocRecord(const std::string& recordName) const;

    bool getRecordData(const std::string& recordName, std::vector<char>& data);
    bool getRecordData(RecordId recordId, std::vector<char>& data);

    /** Returns names from names list record. */
    std::vector<std::string> getNames() const;

    // protected:
    /**
     * Reads and checks if MQDB file header is correct.
     * Throws std::runtime_error exception if not.
     */
    void checkFileHeader(std::ifstream& file);

    /**
     * Reads and caches table of contents records.
     * Throws std::runtime_error exception in case of errors or duplicates.
     */
    void readTableOfContents(std::ifstream& file);

    /**
     * Reads and caches names list contents.
     * Throws std::runtime_error exception in case of errors.
     */
    void readNameList(std::ifstream& file);

    /**
     * Reads and caches contents of '-INDEX.OPT' MQRC record, if present.
     * Throws std::runtime_error exception in case of errors.
     */
    void readIndex(std::ifstream& file);

    /**
     * Reads and caches contents of '-IMAGES.OPT' MQRC record, if present.
     * Throws std::runtime_error exception in case of errors.
     */
    void readImages(std::ifstream& file);

    bool getRecordData(const TocRecord& record, std::vector<char>& data);

    std::map<RecordId, TocRecord> tableOfContents;
    std::map<std::string, RecordId> recordNames;

    IndexData indexData;

    std::map<RelativeOffset, PackedImage> packedImages;

    std::string ffFilePath;
};

#endif 