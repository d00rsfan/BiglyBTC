// SPDX-License-Identifier: GPL-2.0-or-later
package com.biglybt.core.tag.impl;
import com.biglybt.core.torrent.*;import com.biglybt.core.util.*;import java.nio.file.*;import java.net.URL;import java.util.*;
/** Actual torrent creator/parser/getHash APIs. Canonical tag text is encoded
 * from actual selected hash; no live DownloadManager/foreign profile is started. */
public final class OriginalCanonicalTagHashes {
 @SuppressWarnings("unchecked") public static void main(String[] args)throws Exception{
  Path work=Paths.get(args[1]);Files.createDirectories(work);byte[] payload=new byte[17001];for(int i=0;i<payload.length;i++)payload[i]=(byte)(i*31+17);Path file=work.resolve("generated-source.bin");Files.write(file,payload);List<Object> rows=new ArrayList<>();
  for(int kind:new int[]{TOTorrent.TT_V1,TOTorrent.TT_V2,TOTorrent.TT_V1_V2}){
   TOTorrent created=TOTorrentFactory.createFromFileOrDirWithFixedPieceLength(kind,file.toFile(),new URL("http://127.0.0.1/announce"),true,16384).create();Map<String,Object> encoded=created.serialiseToMap();
   long[] selectors=kind==TOTorrent.TT_V1_V2?new long[]{Long.MIN_VALUE,0,1,2}:new long[]{0,1};
   for(long selector:selectors){Map<String,Object> root=new TreeMap<>();root.put("info",encoded.get("info"));root.put("announce","http://127.0.0.1/announce");if(encoded.containsKey("piece layers"))root.put("piece layers",encoded.get("piece layers"));Map<String,Object> props=new TreeMap<>();props.put("hybrid_hash_v2",selector);root.put("azureus_private_properties",props);byte[] bytes=BEncoder.encode(root);TOTorrent parsed=TOTorrentFactory.deserialiseFromBEncodedByteArray(new TOTorrentFactory.TorrentDataHolder(bytes));byte[] selected=parsed.getHash();if(selected.length!=20)throw new AssertionError("Canonical original identity is not20 bytes");Map<String,Object> row=new TreeMap<>();row.put("kind",(long)kind);row.put("selector",selector);row.put("metainfo",bytes);row.put("selected",selected);row.put("tag_text",Base32.encode(selected));byte[] v1=parsed.getFullHash(TOTorrent.TT_V1),v2=parsed.getFullHash(TOTorrent.TT_V2);if(v1!=null)row.put("v1",v1);if(v2!=null)row.put("v2",v2);rows.add(row);}
  }
  Map<String,Object> output=new TreeMap<>();output.put("rows",rows);Files.write(Paths.get(args[0]),BEncoder.encode(output));System.out.println("ORIGINAL_CANONICAL_TAG_HASHES rows="+rows.size()+" actual_creator_parser_selected_full_hashes=1 canonical_encoding_boundary=1 PASS");com.biglybt.core.security.SESecurityManager.exitVM(0);
 }
}
