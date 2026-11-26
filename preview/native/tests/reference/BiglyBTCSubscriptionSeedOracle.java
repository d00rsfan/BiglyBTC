// SPDX-License-Identifier: GPL-2.0-or-later
// Java 17 oracle. Compile against BiglyBT cb8c6cb9bb97916cbc7e80338a017200ffc3e71f.
// Run with a disposable azureus.config.path, output directory and optional payload length.
package com.biglybt.core.subs.impl;

import com.biglybt.core.security.CryptoECCUtils;
import com.biglybt.core.torrent.TOTorrent;
import com.biglybt.core.torrent.TOTorrentFactory;
import com.biglybt.core.util.BEncoder;
import com.biglybt.core.util.SHA1Simple;
import com.biglybt.core.util.protocol.dht.Handler;
import com.biglybt.core.vuzefile.VuzeFileComponent;
import com.biglybt.core.vuzefile.VuzeFileHandler;
import com.biglybt.pifimpl.local.ddb.DDBaseTTTorrent;
import com.biglybt.pifimpl.local.torrent.TorrentImpl;
import org.gudy.bouncycastle.jce.provider.BouncyCastleProvider;

import java.net.URL;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.Security;
import java.util.Arrays;
import java.util.HashMap;
import java.util.HexFormat;
import java.util.List;
import java.util.Map;

public final class BiglyBTCSubscriptionSeedOracle {
    private static byte[] sha1(byte[] bytes) { return new SHA1Simple().calculateHash(bytes); }

    private static final class Transfer extends DDBaseTTTorrent {
        private Transfer() { super(null); }
        byte[] encode(byte[] hash, byte[] data) { return encrypt(hash, data); }
    }

    public static void main(String[] args) throws Exception {
        Security.addProvider(new BouncyCastleProvider());
        Path output = Path.of(args[0]);
        int payloadSize = args.length > 1 ? Integer.parseInt(args[1]) : 310000;
        for (boolean backwards : new boolean[]{false, true}) {
            Path test = Files.createDirectories(output.resolve(backwards ? "old" : "new"));
            var keys = CryptoECCUtils.createKeys();
            byte[] pub = CryptoECCUtils.keyToRawdata(keys.getPublic());
            byte[] priv = CryptoECCUtils.keyToRawdata(keys.getPrivate());
            Map<String, Object> details = new HashMap<>();
            details.put("name", "Seed Oracle".getBytes(StandardCharsets.UTF_8));
            details.put("public_key", pub);
            details.put("version", 4L);
            details.put("is_public", 1L);
            details.put("is_anonymous", 0L);
            details.put("json", ("{\"description\":\"" + "a".repeat(payloadSize) + "\"}").getBytes(StandardCharsets.UTF_8));
            details.put("az_version", 1L);
            var signedDetails = new HashMap<>(details);
            if (backwards) signedDetails.remove("az_version");
            byte[] signed = BEncoder.encode(signedDetails);
            byte[] hash = sha1(signed);
            byte[] signature = SubscriptionBodyImpl.sign(priv, hash, 4, signed.length);
            var body = Map.<String, Object>of("details", details, "hash", hash, "sig", signature, "size", (long) signed.length);
            var verified = new SubscriptionBodyImpl(null, body);
            if (!Arrays.equals(hash, verified.getHash())) throw new IllegalStateException("Java did not verify the signed fixture");
            String sid = HexFormat.of().withUpperCase().formatHex(SubscriptionBodyImpl.deriveShortID(pub, null));
            Files.write(test.resolve("subscriptions.config"), BEncoder.encode(Map.of("subs", List.of(
                Map.of("subscribed", 1L, "public_key", pub, "version", 4L, "hash", hash, "is_anonymous", 0L,
                    "sig", signature, "sig_data_size", (long) signed.length, "pop", 1L, "is_public", 1L,
                    "name", details.get("name"))))));
            var vuze = VuzeFileHandler.getSingleton().create();
            vuze.addComponent(VuzeFileComponent.COMP_TYPE_SUBSCRIPTION, body);
            Path payload = test.resolve(VuzeFileHandler.getVuzeFileName(sid) + ".4");
            vuze.write(payload.toFile());
            TOTorrent torrent = TOTorrentFactory.createFromFileOrDirWithFixedPieceLength(payload.toFile(),
                new URL(null, "dht://", new Handler()), 256 * 1024).create();
            byte[] normalHash = torrent.getHash();
            torrent.setHashOverride(hash);
            var transferable = new TorrentImpl(torrent).removeAdditionalProperties();
            transferable.setDecentralisedBackupRequested(true);
            byte[] torrentBytes = transferable.writeToBEncodedData();
            byte[] encrypted = new Transfer().encode(hash, torrentBytes);
            if (encrypted == null || encrypted[0] != 1 || encrypted[1] != 1) throw new IllegalStateException("Expected encrypted DDB wire response");
            var decoded = TOTorrentFactory.deserialiseFromBEncodedByteArray(new TOTorrentFactory.TorrentDataHolder(torrentBytes));
            if (!Arrays.equals(decoded.getHash(), hash) || Arrays.equals(normalHash, hash)) throw new IllegalStateException("Hash override not retained");
            Files.write(test.resolve("publication.torrent"), torrentBytes);
            Files.write(test.resolve("ddb-response.bin"), encrypted);
            Files.write(test.resolve("publication.hash"), hash);
            Files.writeString(test.resolve("lookup.hex"), HexFormat.of().formatHex(sha1(hash)), StandardCharsets.UTF_8);
            System.out.println((backwards ? "legacy" : "current") + " signature, " + decoded.getPieces().length
                + " payload pieces, versioned .biglybt, retained hash override and encrypted DDB response");
        }
    }
}
