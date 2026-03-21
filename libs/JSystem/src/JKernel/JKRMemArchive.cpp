#include "JSystem/JSystem.h" // IWYU pragma: keep

#include "JSystem/JKernel/JKRMemArchive.h"
#include "JSystem/JKernel/JKRDecomp.h"
#include "JSystem/JKernel/JKRDvdRipper.h"
#include "JSystem/JUtility/JUTAssert.h"
#include "JSystem/JUtility/JUTException.h"
#include <cstring>
#include "global.h"
#include <stdint.h>

JKRMemArchive::JKRMemArchive(s32 entryNum, JKRArchive::EMountDirection mountDirection)
    : JKRArchive(entryNum, MOUNT_MEM) {
    mIsMounted = false;
    mMountDirection = mountDirection;
    if (!open(entryNum, mMountDirection)) {
        return;
    }

    mVolumeType = 'RARC';
    mVolumeName = mStringTable + mNodes->name_offset;

    sVolumeList.prepend(&mFileLoaderLink);
    mIsMounted = true;
}

JKRMemArchive::JKRMemArchive(void* buffer, u32 bufferSize, JKRMemBreakFlag param_3)
    : JKRArchive((uintptr_t)buffer, MOUNT_MEM) {
    mIsMounted = false;
    if (!open(buffer, bufferSize, param_3)) {
        return;
    }

    mVolumeType = 'RARC';
    mVolumeName = mStringTable + mNodes->name_offset;

    sVolumeList.prepend(&mFileLoaderLink);
    mIsMounted = true;
}

JKRMemArchive::~JKRMemArchive() {
    if (mIsMounted == true) {
        if (mIsOpen) {
            if (mArcHeader)
                JKRFreeToHeap(mHeap, mArcHeader);
        }

        sVolumeList.remove(&mFileLoaderLink);
        mIsMounted = false;
    }
}

static void dummy() {
    OS_REPORT(__FILE__);
    OS_REPORT("isMounted()");
    OS_REPORT("mMountCount == 1");
}

bool JKRMemArchive::open(s32 entryNum, JKRArchive::EMountDirection mountDirection) {
    mArcHeader = NULL;
    mArcInfoBlock = NULL;
    mArchiveData = NULL;
    mNodes = NULL;
    mFiles = NULL;
    mStringTable = NULL;
    mIsOpen = false;
    mMountDirection = mountDirection;

    if (mMountDirection == JKRArchive::MOUNT_DIRECTION_HEAD) {
        u32 loadedSize;
        mArcHeader = (SArcHeader *)JKRDvdToMainRam(
            entryNum, NULL, EXPAND_SWITCH_UNKNOWN1, 0, mHeap, JKRDvdRipper::ALLOC_DIRECTION_FORWARD,
            0, (int *)&mCompression, &loadedSize);
        if (mArcHeader) {
            DCInvalidateRange(mArcHeader, loadedSize);
        }
    }
    else {
        u32 loadedSize;
        mArcHeader = (SArcHeader *)JKRDvdToMainRam(
            entryNum, NULL, EXPAND_SWITCH_UNKNOWN1, 0, mHeap,
            JKRDvdRipper::ALLOC_DIRECTION_BACKWARD, 0, (int *)&mCompression, &loadedSize);
        if (mArcHeader) {
            DCInvalidateRange(mArcHeader, loadedSize);
        }
    }

    if (!mArcHeader) {
        mMountMode = UNKNOWN_MOUNT_MODE;
    }
    else {
#ifdef TARGET_PC
        /* Debug: print header + data info */
        if (0) fprintf(stderr, "[PC] JKRMemArchive::open(entryNum=%d): compression=%d\n",
                entryNum, mCompression);
        {
            u8* raw = (u8*)mArcHeader;
            u32 hdrLen = read_big_endian_u32(&((u32*)raw)[2]);
            u8* info = raw + hdrLen;
            if (0) fprintf(stderr, "[PC]   ArcDataInfo @ +%u: %02x%02x%02x%02x %02x%02x%02x%02x\n",
                    hdrLen, info[0], info[1], info[2], info[3], info[4], info[5], info[6], info[7]);
        }
        /* Byte-swap SArcHeader from big-endian disc data */
        mArcHeader->signature = read_big_endian_u32(&mArcHeader->signature);
        mArcHeader->file_length = read_big_endian_u32(&mArcHeader->file_length);
        mArcHeader->header_length = read_big_endian_u32(&mArcHeader->header_length);
        mArcHeader->file_data_offset = read_big_endian_u32(&mArcHeader->file_data_offset);
        mArcHeader->file_data_length = read_big_endian_u32(&mArcHeader->file_data_length);
        mArcHeader->field_0x14 = read_big_endian_u32(&mArcHeader->field_0x14);
        mArcHeader->field_0x18 = read_big_endian_u32(&mArcHeader->field_0x18);
        mArcHeader->field_0x1c = read_big_endian_u32(&mArcHeader->field_0x1c);
#endif
        JUT_ASSERT(438, mArcHeader->signature == 'RARC');
        mArcInfoBlock = (SArcDataInfo *)((u8 *)mArcHeader + mArcHeader->header_length);
#ifdef TARGET_PC
        /* Byte-swap SArcDataInfo */
        mArcInfoBlock->num_nodes = read_big_endian_u32(&mArcInfoBlock->num_nodes);
        mArcInfoBlock->node_offset = read_big_endian_u32(&mArcInfoBlock->node_offset);
        mArcInfoBlock->num_file_entries = read_big_endian_u32(&mArcInfoBlock->num_file_entries);
        mArcInfoBlock->file_entry_offset = read_big_endian_u32(&mArcInfoBlock->file_entry_offset);
        mArcInfoBlock->string_table_length = read_big_endian_u32(&mArcInfoBlock->string_table_length);
        mArcInfoBlock->string_table_offset = read_big_endian_u32(&mArcInfoBlock->string_table_offset);
        mArcInfoBlock->next_free_file_id = read_big_endian_u16(&mArcInfoBlock->next_free_file_id);
#endif
        mNodes = (SDIDirEntry *)((u8 *)&mArcInfoBlock->num_nodes + mArcInfoBlock->node_offset);
        mStringTable = (char *)((u8 *)&mArcInfoBlock->num_nodes + mArcInfoBlock->string_table_offset);
#ifdef TARGET_PC
        /* Byte-swap directory nodes */
        for (u32 i = 0; i < mArcInfoBlock->num_nodes; i++) {
            mNodes[i].type = read_big_endian_u32(&mNodes[i].type);
            mNodes[i].name_offset = read_big_endian_u32(&mNodes[i].name_offset);
            mNodes[i].field_0x8 = read_big_endian_u16(&mNodes[i].field_0x8);
            mNodes[i].num_entries = read_big_endian_u16(&mNodes[i].num_entries);
            mNodes[i].first_file_index = read_big_endian_u32(&mNodes[i].first_file_index);
        }
        /* File entries on disc are 20 bytes each (no pointer).
         * SDIFileEntry on 64-bit is 24 bytes (has void* data).
         * Convert from disc format to memory format. */
        {
            struct SDIFileEntryDisc { u16 file_id; u16 name_hash; u32 type_flags_and_name_offset; u32 data_offset; u32 data_size; u32 pad; };
            SDIFileEntryDisc* disc_files = (SDIFileEntryDisc*)((u8 *)&mArcInfoBlock->num_nodes + mArcInfoBlock->file_entry_offset);
            u32 num_entries = mArcInfoBlock->num_file_entries;
            SDIFileEntry* new_files = (SDIFileEntry*)malloc(num_entries * sizeof(SDIFileEntry));
            for (u32 i = 0; i < num_entries; i++) {
                new_files[i].file_id = read_big_endian_u16(&disc_files[i].file_id);
                new_files[i].name_hash = read_big_endian_u16(&disc_files[i].name_hash);
                new_files[i].type_flags_and_name_offset = read_big_endian_u32(&disc_files[i].type_flags_and_name_offset);
                new_files[i].data_offset = read_big_endian_u32(&disc_files[i].data_offset);
                new_files[i].data_size = read_big_endian_u32(&disc_files[i].data_size);
                new_files[i].data = NULL;
            }
            mFiles = new_files;
        }
#else
        mFiles = (SDIFileEntry *)((u8 *)&mArcInfoBlock->num_nodes + mArcInfoBlock->file_entry_offset);
#endif

        mArchiveData =
            (u8 *)((uintptr_t)mArcHeader + mArcHeader->header_length + mArcHeader->file_data_offset);
#ifdef TARGET_PC
        if (0) fprintf(stderr, "[PC] JKRMemArchive: mArcHeader=%p archiveData=%p (hdr+%u+%u), bytes: %02x%02x%02x%02x %02x%02x%02x%02x\n",
                (void*)mArcHeader,
                mArchiveData, mArcHeader->header_length, mArcHeader->file_data_offset,
                mArchiveData[0], mArchiveData[1], mArchiveData[2], mArchiveData[3],
                mArchiveData[4], mArchiveData[5], mArchiveData[6], mArchiveData[7]);
#endif
        mIsOpen = true;
    }

#if DEBUG
    if (mMountMode == 0) {
        OSReport(":::Cannot alloc memory [%s][%d]\n", __FILE__, 460);
    }
#endif

    return (mMountMode == UNKNOWN_MOUNT_MODE) ? false : true;
}

bool JKRMemArchive::open(void* buffer, u32 bufferSize, JKRMemBreakFlag flag) {
    mArcHeader = (SArcHeader *)buffer;
#ifdef TARGET_PC
    mArcHeader->signature = read_big_endian_u32(&mArcHeader->signature);
    mArcHeader->file_length = read_big_endian_u32(&mArcHeader->file_length);
    mArcHeader->header_length = read_big_endian_u32(&mArcHeader->header_length);
    mArcHeader->file_data_offset = read_big_endian_u32(&mArcHeader->file_data_offset);
    mArcHeader->file_data_length = read_big_endian_u32(&mArcHeader->file_data_length);
    mArcHeader->field_0x14 = read_big_endian_u32(&mArcHeader->field_0x14);
    mArcHeader->field_0x18 = read_big_endian_u32(&mArcHeader->field_0x18);
    mArcHeader->field_0x1c = read_big_endian_u32(&mArcHeader->field_0x1c);
#endif
    JUT_ASSERT(491, mArcHeader->signature == 'RARC');
    mArcInfoBlock = (SArcDataInfo *)((u8 *)mArcHeader + mArcHeader->header_length);
#ifdef TARGET_PC
    mArcInfoBlock->num_nodes = read_big_endian_u32(&mArcInfoBlock->num_nodes);
    mArcInfoBlock->node_offset = read_big_endian_u32(&mArcInfoBlock->node_offset);
    mArcInfoBlock->num_file_entries = read_big_endian_u32(&mArcInfoBlock->num_file_entries);
    mArcInfoBlock->file_entry_offset = read_big_endian_u32(&mArcInfoBlock->file_entry_offset);
    mArcInfoBlock->string_table_length = read_big_endian_u32(&mArcInfoBlock->string_table_length);
    mArcInfoBlock->string_table_offset = read_big_endian_u32(&mArcInfoBlock->string_table_offset);
    mArcInfoBlock->next_free_file_id = read_big_endian_u16(&mArcInfoBlock->next_free_file_id);
#endif
    mNodes = (SDIDirEntry *)((u8 *)&mArcInfoBlock->num_nodes + mArcInfoBlock->node_offset);
    mStringTable = (char *)((u8 *)&mArcInfoBlock->num_nodes + mArcInfoBlock->string_table_offset);
#ifdef TARGET_PC
    for (u32 i = 0; i < mArcInfoBlock->num_nodes; i++) {
        mNodes[i].type = read_big_endian_u32(&mNodes[i].type);
        mNodes[i].name_offset = read_big_endian_u32(&mNodes[i].name_offset);
        mNodes[i].field_0x8 = read_big_endian_u16(&mNodes[i].field_0x8);
        mNodes[i].num_entries = read_big_endian_u16(&mNodes[i].num_entries);
        mNodes[i].first_file_index = read_big_endian_u32(&mNodes[i].first_file_index);
    }
    {
        struct SDIFileEntryDisc { u16 file_id; u16 name_hash; u32 type_flags_and_name_offset; u32 data_offset; u32 data_size; u32 pad; };
        SDIFileEntryDisc* disc_files = (SDIFileEntryDisc*)((u8 *)&mArcInfoBlock->num_nodes + mArcInfoBlock->file_entry_offset);
        u32 num_entries = mArcInfoBlock->num_file_entries;
        SDIFileEntry* new_files = (SDIFileEntry*)malloc(num_entries * sizeof(SDIFileEntry));
        for (u32 i = 0; i < num_entries; i++) {
            new_files[i].file_id = read_big_endian_u16(&disc_files[i].file_id);
            new_files[i].name_hash = read_big_endian_u16(&disc_files[i].name_hash);
            new_files[i].type_flags_and_name_offset = read_big_endian_u32(&disc_files[i].type_flags_and_name_offset);
            new_files[i].data_offset = read_big_endian_u32(&disc_files[i].data_offset);
            new_files[i].data_size = read_big_endian_u32(&disc_files[i].data_size);
            new_files[i].data = NULL;
        }
        mFiles = new_files;
    }
#else
    mFiles = (SDIFileEntry *)((u8 *)&mArcInfoBlock->num_nodes + mArcInfoBlock->file_entry_offset);
#endif
    mArchiveData = (u8 *)(((uintptr_t)mArcHeader + mArcHeader->header_length) + mArcHeader->file_data_offset);
    mIsOpen = (flag == JKRMEMBREAK_FLAG_UNKNOWN1) ? true : false;
    mHeap = JKRHeap::findFromRoot(buffer);
    mCompression = COMPRESSION_NONE;
    return true;
}

void* JKRMemArchive::fetchResource(SDIFileEntry* fileEntry, u32* resourceSize) {
    JUT_ASSERT(555, isMounted());
    if (!fileEntry->data) {
        fileEntry->data = mArchiveData + fileEntry->data_offset;
    }

    if (resourceSize) {
        *resourceSize = fileEntry->data_size;
    }

    return fileEntry->data;
}

void* JKRMemArchive::fetchResource(void* buffer, u32 bufferSize, SDIFileEntry* fileEntry,
                                   u32* resourceSize) {
    JUT_ASSERT(595, isMounted());
    u32 srcLength = fileEntry->data_size;
    if (srcLength > bufferSize) {
        srcLength = bufferSize;
    }

    if (fileEntry->data != NULL) {
        memcpy(buffer, fileEntry->data, srcLength);
    } else {
        u8 flags = fileEntry->type_flags_and_name_offset >> 24;
        JKRCompression compression = JKRConvertAttrToCompressionType(flags);
        srcLength =
            fetchResource_subroutine(mArchiveData + fileEntry->data_offset, srcLength, (u8*)buffer, bufferSize, compression);
    }

    if (resourceSize) {
        *resourceSize = srcLength;
    }

    return buffer;
}

void JKRMemArchive::removeResourceAll(void) {
    JUT_ASSERT(642, isMounted());

    if (mArcInfoBlock == NULL)
        return;
    if (mMountMode == MOUNT_MEM)
        return;

    // !@bug: looping over file entries without incrementing the fileEntry pointer. Thus, only the
    // first fileEntry will clear/remove the resource data.
    SDIFileEntry* fileEntry = mFiles;
    for (int i = 0; i < mArcInfoBlock->num_file_entries; i++) {
        if (fileEntry->data) {
            fileEntry->data = NULL;
        }
    }
    fileEntry++;
}

bool JKRMemArchive::removeResource(void* resource) {
    JUT_ASSERT(673, isMounted());

    SDIFileEntry* fileEntry = findPtrResource(resource);
    if (!fileEntry)
        return false;

    fileEntry->data = NULL;
    return true;
}

u32 JKRMemArchive::fetchResource_subroutine(u8* src, u32 srcLength, u8* dst, u32 dstLength,
                                            JKRCompression compression) {
    switch (compression) {
    case COMPRESSION_NONE:
        if (srcLength > dstLength) {
            srcLength = dstLength;
        }

        memcpy(dst, src, srcLength);
        return srcLength;

    case COMPRESSION_YAY0:
    case COMPRESSION_YAZ0: {
        u32 expendedSize = JKRDecompExpandSize(src);
        if (expendedSize > dstLength) {
            expendedSize = dstLength;
        }

        JKRDecompress(src, dst, expendedSize, 0);
        return expendedSize;
    }

    default: {
        JUTException::panic(__FILE__, 723, "??? bad sequence\n");
    } break;
    }

    return 0;
}

u32 JKRMemArchive::getExpandedResSize(const void* resource) const {
    SDIFileEntry* fileEntry = findPtrResource(resource);
    if (fileEntry == NULL)
        return -1;

    u8 flags = fileEntry->type_flags_and_name_offset >> 24;
    if ((flags & 4) == false) {
        return getResSize(resource);
    }
    u32 expandSize = JKRDecompExpandSize((u8*)resource);
    return expandSize;
}
