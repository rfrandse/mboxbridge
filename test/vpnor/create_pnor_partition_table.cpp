// SPDX-License-Identifier: Apache-2.0
// Copyright (C) 2018 IBM Corp.
#include "pnor_partition_table.hpp"
#include "config.h"
#include <assert.h>
#include <string.h>
#include <vector>
#include <fstream>
#include <experimental/filesystem>

static const auto BLOCK_SIZE = 4 * 1024;
static const auto PNOR_SIZE = 64 * 1024 * 1024;

const std::string toc[] = {
    "partition01=HBB,00000000,00000400,80,ECC,PRESERVED",
};
constexpr auto partitionName = "HBB";

namespace fs = std::experimental::filesystem;

template <std::size_t N>
static void createVpnorTree(fs::path& root, const std::string (&toc)[N],
                            size_t blockSize)
{
    fs::path tocFilePath{root};
    tocFilePath /= PARTITION_TOC_FILE;
    std::ofstream tocFile(tocFilePath.c_str());

    for (const std::string& line : toc)
    {
        pnor_partition part;

        openpower::virtual_pnor::parseTocLine(line, blockSize, part);

        /* Populate the partition in the tree */
        fs::path partitionFilePath{root};
        partitionFilePath /= part.data.name;
        std::ofstream partitionFile(partitionFilePath.c_str());
        std::vector<char> empty(part.data.size, 0);
        partitionFile.write(empty.data(), empty.size());
        partitionFile.close();

        /* Update the ToC if the partition file was created */
        tocFile.write(line.c_str(), line.length());
        tocFile.write("\n", 1);
    }

    tocFile.close();
}

int main()
{
    char tmplt[] = "/tmp/vpnor_partitions.XXXXXX";
    char* tmpdir = mkdtemp(tmplt);
    assert(tmpdir != nullptr);
    fs::path root{tmpdir};

    createVpnorTree(root, toc, BLOCK_SIZE);

    const openpower::virtual_pnor::partition::Table table(
        fs::path{tmpdir}, BLOCK_SIZE, PNOR_SIZE);

    pnor_partition_table expectedTable{};
    expectedTable.data.magic = PARTITION_HEADER_MAGIC;
    expectedTable.data.version = PARTITION_VERSION_1;
    expectedTable.data.size = 1; // 1 block
    expectedTable.data.entry_size = sizeof(pnor_partition);
    expectedTable.data.entry_count = 1; // 1 partition
    expectedTable.data.block_size = BLOCK_SIZE;
    expectedTable.data.block_count =
        (PNOR_SIZE) / expectedTable.data.block_size;
    expectedTable.checksum =
        openpower::virtual_pnor::details::checksum(expectedTable.data);

    pnor_partition expectedPartition{};
    strcpy(expectedPartition.data.name, partitionName);
    expectedPartition.data.base = 0;       // starts at offset 0
    expectedPartition.data.size = 1;       // 1 block
    expectedPartition.data.actual = 0x400; // 1024 bytes
    expectedPartition.data.id = 1;
    expectedPartition.data.pid = PARENT_PATITION_ID;
    expectedPartition.data.type = PARTITION_TYPE_DATA;
    expectedPartition.data.flags = 0;
    expectedPartition.data.user.data[0] = PARTITION_ECC_PROTECTED;
    expectedPartition.data.user.data[1] |= PARTITION_PRESERVED;
    expectedPartition.data.user.data[1] |= PARTITION_VERSION_CHECK_SHA512;
    expectedPartition.checksum =
        openpower::virtual_pnor::details::checksum(expectedPartition.data);

    const pnor_partition_table& result = table.getNativeTable();

    fs::remove_all(fs::path{tmpdir});

    auto rc = memcmp(&expectedTable, &result, sizeof(pnor_partition_table));
    assert(rc == 0);

    rc = memcmp(&expectedPartition, &result.partitions[0],
                sizeof(pnor_partition));
    assert(rc == 0);

    const pnor_partition& first = table.partition(0); // Partition at offset 0
    rc = memcmp(&first, &result.partitions[0], sizeof(pnor_partition));
    assert(rc == 0);

    return 0;
}