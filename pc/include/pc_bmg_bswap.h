#ifndef PC_BMG_BSWAP_H
#define PC_BMG_BSWAP_H

#ifdef TARGET_PC

#include "pc_bswap.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

static inline uint16_t pc_bmg_load_u16(const void* ptr) {
    uint16_t value;
    std::memcpy(&value, ptr, sizeof(value));
    return value;
}

static inline uint32_t pc_bmg_load_u32(const void* ptr) {
    uint32_t value;
    std::memcpy(&value, ptr, sizeof(value));
    return value;
}

static inline void pc_bmg_store_u16(void* ptr, uint16_t value) {
    std::memcpy(ptr, &value, sizeof(value));
}

static inline void pc_bmg_store_u32(void* ptr, uint32_t value) {
    std::memcpy(ptr, &value, sizeof(value));
}

static inline bool pc_bmg_is_known_block(uint32_t magic) {
    switch (magic) {
    case 'INF1':
    case 'DAT1':
    case 'STR1':
    case 'FLW1':
    case 'FLI1':
    case 'MID1':
        return true;
    default:
        return false;
    }
}

static inline bool pc_bmg_needs_bswap(const void* data, size_t data_size) {
    if (data == NULL || data_size < 0x28) {
        return false;
    }

    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    if (std::memcmp(bytes, "MESGbmg1", 8) != 0) {
        return false;
    }

    uint32_t raw_block_magic = pc_bmg_load_u32(bytes + 0x20);
    if (pc_bmg_is_known_block(raw_block_magic)) {
        return false;
    }

    if (pc_bmg_is_known_block(pc_bswap32(raw_block_magic))) {
        return true;
    }

    uint32_t raw_size = pc_bmg_load_u32(bytes + 0x08);
    uint32_t swapped_size = pc_bswap32(raw_size);
    return raw_size > data_size && swapped_size <= data_size;
}

static inline void pc_bmg_bswap_inf1(uint8_t* section, size_t section_size) {
    if (section_size < 0x10) {
        return;
    }

    uint16_t entry_count = pc_bswap16(pc_bmg_load_u16(section + 0x08));
    uint16_t entry_size = pc_bswap16(pc_bmg_load_u16(section + 0x0A));
    pc_bmg_store_u16(section + 0x08, entry_count);
    pc_bmg_store_u16(section + 0x0A, entry_size);

    if (section_size >= 0x0E) {
        uint16_t group_id = pc_bswap16(pc_bmg_load_u16(section + 0x0C));
        pc_bmg_store_u16(section + 0x0C, group_id);
    }

    if (entry_size == 0) {
        return;
    }

    size_t max_entries = (section_size - 0x10) / entry_size;
    if (entry_count > max_entries) {
        entry_count = static_cast<uint16_t>(max_entries);
    }

    for (uint16_t i = 0; i < entry_count; ++i) {
        uint8_t* entry = section + 0x10 + i * entry_size;
        pc_bmg_store_u32(entry + 0x00, pc_bswap32(pc_bmg_load_u32(entry + 0x00)));

        if (entry_size == 0x14) {
            pc_bmg_store_u16(entry + 0x04, pc_bswap16(pc_bmg_load_u16(entry + 0x04)));
            pc_bmg_store_u16(entry + 0x06, pc_bswap16(pc_bmg_load_u16(entry + 0x06)));
            pc_bmg_store_u16(entry + 0x12, pc_bswap16(pc_bmg_load_u16(entry + 0x12)));
        } else {
            for (size_t off = 0x04; off + 1 < entry_size; off += 2) {
                pc_bmg_store_u16(entry + off, pc_bswap16(pc_bmg_load_u16(entry + off)));
            }
        }
    }
}

static inline void pc_bmg_bswap_flw1(uint8_t* section, size_t section_size) {
    if (section_size < 0x10) {
        return;
    }

    uint16_t node_count = pc_bswap16(pc_bmg_load_u16(section + 0x08));
    uint16_t aux_count = pc_bswap16(pc_bmg_load_u16(section + 0x0A));
    pc_bmg_store_u16(section + 0x08, node_count);
    pc_bmg_store_u16(section + 0x0A, aux_count);

    size_t max_nodes = (section_size - 0x10) / 8;
    if (node_count > max_nodes) {
        node_count = static_cast<uint16_t>(max_nodes);
    }

    for (uint16_t i = 0; i < node_count; ++i) {
        uint8_t* node = section + 0x10 + i * 8;
        pc_bmg_store_u16(node + 0x02, pc_bswap16(pc_bmg_load_u16(node + 0x02)));
        pc_bmg_store_u16(node + 0x04, pc_bswap16(pc_bmg_load_u16(node + 0x04)));
        pc_bmg_store_u16(node + 0x06, pc_bswap16(pc_bmg_load_u16(node + 0x06)));
    }

    size_t tail_off = 0x10 + node_count * 8;
    if (tail_off >= section_size) {
        return;
    }

    size_t tail_words = (section_size - tail_off) / sizeof(uint16_t);
    for (size_t i = 0; i < tail_words; ++i) {
        uint8_t* word = section + tail_off + i * sizeof(uint16_t);
        pc_bmg_store_u16(word, pc_bswap16(pc_bmg_load_u16(word)));
    }
}

static inline void pc_bmg_bswap_fli1(uint8_t* section, size_t section_size) {
    if (section_size < 0x10) {
        return;
    }

    uint16_t entry_count = pc_bswap16(pc_bmg_load_u16(section + 0x08));
    uint16_t aux_count = pc_bswap16(pc_bmg_load_u16(section + 0x0A));
    pc_bmg_store_u16(section + 0x08, entry_count);
    pc_bmg_store_u16(section + 0x0A, aux_count);

    size_t max_entries = (section_size - 0x10) / 8;
    if (entry_count > max_entries) {
        entry_count = static_cast<uint16_t>(max_entries);
    }

    for (uint16_t i = 0; i < entry_count; ++i) {
        uint8_t* entry = section + 0x10 + i * 8;
        pc_bmg_store_u32(entry + 0x00, pc_bswap32(pc_bmg_load_u32(entry + 0x00)));
        pc_bmg_store_u16(entry + 0x04, pc_bswap16(pc_bmg_load_u16(entry + 0x04)));
        pc_bmg_store_u16(entry + 0x06, pc_bswap16(pc_bmg_load_u16(entry + 0x06)));
    }
}

static inline void pc_bmg_bswap_mid1(uint8_t* section, size_t section_size) {
    if (section_size < 0x10) {
        return;
    }

    uint16_t entry_count = pc_bswap16(pc_bmg_load_u16(section + 0x08));
    pc_bmg_store_u16(section + 0x08, entry_count);

    size_t max_entries = (section_size - 0x10) / sizeof(uint32_t);
    if (entry_count > max_entries) {
        entry_count = static_cast<uint16_t>(max_entries);
    }

    for (uint16_t i = 0; i < entry_count; ++i) {
        uint8_t* entry = section + 0x10 + i * sizeof(uint32_t);
        pc_bmg_store_u32(entry, pc_bswap32(pc_bmg_load_u32(entry)));
    }
}

static inline void pc_bmg_bswap_file(void* data, size_t data_size) {
    if (!pc_bmg_needs_bswap(data, data_size)) {
        return;
    }

    uint8_t* bytes = static_cast<uint8_t*>(data);

    pc_bmg_store_u32(bytes + 0x00, pc_bswap32(pc_bmg_load_u32(bytes + 0x00)));
    pc_bmg_store_u32(bytes + 0x04, pc_bswap32(pc_bmg_load_u32(bytes + 0x04)));

    uint32_t file_size = pc_bswap32(pc_bmg_load_u32(bytes + 0x08));
    uint32_t block_count = pc_bswap32(pc_bmg_load_u32(bytes + 0x0C));
    pc_bmg_store_u32(bytes + 0x08, file_size);
    pc_bmg_store_u32(bytes + 0x0C, block_count);

    fprintf(stderr, "[BMG] swapped message resource (%zu bytes)\n", data_size);

    size_t offset = 0x20;
    for (uint32_t i = 0; i < block_count && offset + 8 <= data_size; ++i) {
        uint8_t* section = bytes + offset;
        uint32_t magic = pc_bswap32(pc_bmg_load_u32(section + 0x00));
        uint32_t size = pc_bswap32(pc_bmg_load_u32(section + 0x04));
        pc_bmg_store_u32(section + 0x00, magic);
        pc_bmg_store_u32(section + 0x04, size);

        if (size < 8 || offset + size > data_size) {
            break;
        }

        switch (magic) {
        case 'INF1':
            pc_bmg_bswap_inf1(section, size);
            break;
        case 'FLW1':
            pc_bmg_bswap_flw1(section, size);
            break;
        case 'FLI1':
            pc_bmg_bswap_fli1(section, size);
            break;
        case 'MID1':
            pc_bmg_bswap_mid1(section, size);
            break;
        default:
            break;
        }

        offset += size;
    }
}

#endif

#endif
