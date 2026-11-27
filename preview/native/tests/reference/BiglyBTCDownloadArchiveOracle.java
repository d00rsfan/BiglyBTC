// SPDX-License-Identifier: GPL-2.0-or-later
// Compile against BiglyBT cb8c6cb9bb97916cbc7e80338a017200ffc3e71f.
// Run with a disposable azureus.config.path and a fixture output file.
package com.biglybt.pifimpl.local.download;

import com.biglybt.core.util.BEncoder;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.Map;
import java.util.List;

public final class BiglyBTCDownloadArchiveOracle {
    static DownloadStubImpl stub(int hashLength, String name, long size, long archived, long completed) {
        byte[] hash = new byte[hashLength];
        hash[0] = (byte)hashLength;
        Map<String,Object> row = new HashMap<>();
        row.put("hash", hash);
        row.put("name", name);
        row.put("l", "/nonexistent/Archive Ω");
        row.put("dt", archived);
        row.put("dc", completed);
        row.put("s", size);
        row.put("sr", 1245L);
        row.put("gm", Map.of("persistent", 1L, "hash", hash));
        row.put("tr", "http://tracker.invalid/announce");
        row.put("cat", "Linux");
        row.put("t", List.of("Kept", "Saved"));
        row.put("files", List.of(Map.of("rel", "part.iso", "len", -1024L),
            Map.of("file", "/nonexistent/linked file", "len", 4096L)));
        return new DownloadStubImpl(null, row);
    }

    public static void main(String[] arguments) throws Exception {
        var older = stub(20, "Open source Ω", 5120L, 1700000000000L, 1699999000000L);
        var newer = stub(32, "Original v2", 8192L, 1700001000000L, 0L);
        if (older.getStubFiles()[0].getLength() != -1024L || newer.getTorrentHash().length != 32
                || older.getCreationDate() >= newer.getCreationDate()) {
            throw new IllegalStateException("Unexpected pinned Java archive getter result");
        }
        Files.write(Path.of(arguments[0]), BEncoder.encode(Map.of("stubs", List.of(older.exportToMap(), newer.exportToMap()),
            "other", "unrelated source data".getBytes(StandardCharsets.UTF_8))));
        System.out.println("Pinned Java DownloadStubImpl getters/export produced two original archive rows");
    }
}
