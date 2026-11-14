# SPDX-License-Identifier: GPL-2.0-or-later
"""Compile unchanged original DATA queue and selected serializer on JDK 17.

Only the loopback Adapter/header/contact seams are generated. No Maven, profile,
desktop, production socket, or substituted transfer algorithm is involved.
"""
import argparse,hashlib,json,re,subprocess
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--original',required=True);p.add_argument('--classes',required=True);p.add_argument('--javac',required=True);p.add_argument('--output',required=True);a=p.parse_args()
root=Path(a.original);out=Path(a.output);out.mkdir(parents=True,exist_ok=True)
packet=root/'core/src/com/biglybt/core/dht/transport/udp/impl/DHTUDPPacketData.java';text=packet.read_text()
def tail_after(anchor):
 start=text.index(anchor)+len(anchor);end=text.index('\n\t}',start);return text[start:end]
read=tail_after('super( network_handler, is,  DHTUDPPacketHelper.ACT_DATA, con_id, trans_id );')
write=tail_after('super.serialise(os);')
body="""package com.biglybt.core.dht.transport.udp.impl;
import java.io.*;import com.biglybt.core.dht.transport.udp.DHTTransportUDP;
public final class OriginalRawDataBody {
 byte packet_type;byte[] transfer_key,key,data;int start_position,length,total_length;
 public byte getProtocolVersion(){return 55;}
 OriginalRawDataBody(){} OriginalRawDataBody(DataInputStream is)throws IOException {
"""+read+"""\n } public void serialise(DataOutputStream os)throws IOException {
"""+write+'\n }\n}\n'
generated=out/'OriginalRawDataBody.java';generated.write_text(body)
handler=root/'core/src/com/biglybt/core/dht/transport/util/DHTTransferHandler.java'
helper=Path(__file__).with_name('OriginalRawWritePeer.java')
subprocess.run([a.javac,'--release','17','-cp',a.classes,'-d',str(out),str(handler),str(generated),str(helper)],check=True)
sha=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
(out/'source-scope.json').write_text(json.dumps({'original':'cb8c6cb9bb97916cbc7e80338a017200ffc3e71f','handler_source_sha256':sha(handler),'packet_source_sha256':sha(packet),'selected_read_sha256':hashlib.sha256(read.encode()).hexdigest(),'selected_write_sha256':hashlib.sha256(write.encode()).hexdigest(),'helper_sha256':sha(helper),'scope':'Unchanged source-built original DHTTransferHandler with selected exact DATA body statements. Owned IPv4 UDP Adapter, literal header and dynamic contact seams; no original full DHT owner, NAT owner, profile or public network exercised.'},indent=2)+'\n')
print('PASS original raw-write peer prepared')
