package org.parg.azureus.plugins.networks.i2p.vuzedht;

import static org.parg.azureus.plugins.networks.i2p.vuzedht.DiscoveryFixture.*;
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.AtomicInteger;
import net.i2p.data.Destination;
import org.parg.azureus.plugins.networks.i2p.snarkdht.*;
import com.biglybt.core.dht.DHT;
import com.biglybt.core.dht.transport.*;

/** Original public transport calls only; no Native grant, packet builder or storage shortcut. */
public final class InboundOriginalPeer {
    static final int CALLBACK_MS=40000,MAX_COMMANDS=24;
    static final Set<String> COMMANDS=Set.of("ping","find","token","seed","read-seed","downloader-token","downloader","read-downloader","bad-token","read-after-bad-token","expect-no-source","expect-expired","expect-closed","expect-router-eof","stop");
    static final class Reply extends DHTTransportReplyHandlerAdapter {
        final CountDownLatch done=new CountDownLatch(1);
        final AtomicInteger callbacks=new AtomicInteger();
        final DHTTransportContactI2P expected;
        volatile String kind="",failure="";
        volatile DHTTransportValue[] values=new DHTTransportValue[0];
        volatile DHTTransportContact[] nodes=new DHTTransportContact[0];
        volatile boolean more,contactMatches=true;volatile byte diversification=DHT.DT_NONE;
        Reply(DHTTransportContactI2P expected){this.expected=expected;}
        synchronized void finish(String next,DHTTransportContact contact){
            callbacks.incrementAndGet();
            if(contact!=expected||!Arrays.equals(contact.getID(),expected.getID())){contactMatches=false;failure="UnexpectedReplyContact";}
            kind=next;done.countDown();
        }
        @Override public void pingReply(DHTTransportContact contact){finish("ping",contact);}
        @Override public void findNodeReply(DHTTransportContact contact,DHTTransportContact[] found){nodes=found.clone();finish("nodes",contact);}
        @Override public void findValueReply(DHTTransportContact contact,DHTTransportContact[] found){nodes=found.clone();finish("nodes",contact);}
        @Override public void findValueReply(DHTTransportContact contact,DHTTransportValue[] found,byte type,boolean continued){values=found.clone();diversification=type;more=continued;finish("values",contact);}
        @Override public void storeReply(DHTTransportContact contact,byte[] types){
            if(types.length!=1||types[0]!=DHT.DT_NONE)failure="UnexpectedStoreReply";
            finish("store",contact);
        }
        @Override public void failed(DHTTransportContact contact,Throwable error){failure=error.getClass().getName();finish("failed",contact);}
        void await(boolean success)throws Exception{
            require(done.await(CALLBACK_MS,TimeUnit.MILLISECONDS),"Actual original transport callback deadline");
            require(callbacks.get()==1,"Actual original callback count differs");
            require(contactMatches,"Actual original callback selected another contact");
            require(success?!kind.equals("failed")&&failure.isEmpty():kind.equals("failed")&&!failure.isEmpty(),"Actual original reply outcome differs");
        }
    }
    static final class Peer {
        final DiscoveryPeer.Node node;
        final DHTTransportI2P transport;
        final DHTTransportContactI2P remote;
        Peer(int i2cp,int port,String name,NodeInfo nativeNode)throws Exception{
            node=new DiscoveryPeer.Node(i2cp,port,"inbound-"+name,null);
            transport=node.dht.getTransport();require(!transport.isDisabled(),"Actual original transport disabled");
            remote=transport.importContact(nativeNode,false);
            require(remote.getNode().equals(nativeNode)&&remote.isValid(),"Actual secure Native contact differs");
        }
    }
    static NodeInfo nativeNode()throws Exception{
        byte[] packed=read("inbound-native.node",NodeInfo.LENGTH);require(packed.length==NodeInfo.LENGTH,"Native NodeInfo must be exactly54 bytes");
        NodeInfo parsed=new NodeInfo(packed,0);Destination destination=destination("inbound-native.dest");
        NodeInfo full=new NodeInfo(parsed.getNID(),destination,parsed.getPort());
        require(Arrays.equals(full.getData(),packed)&&full.getHash().equals(destination.calculateHash()),"Native secure NodeInfo/full Destination mismatch");
        require(full.getPort()>=1024&&full.getPort()<65535,"Native query/reply port bounds");return full;
    }
    static Reply ping(Peer peer,boolean success)throws Exception{
        Reply reply=new Reply(peer.remote);peer.transport.sendPing(reply,peer.remote);reply.await(success);
        require(!success||reply.kind.equals("ping"),"Actual original ping callback differs");return reply;
    }
    static Reply lookup(Peer peer,byte[] target)throws Exception{
        Reply reply=new Reply(peer.remote);peer.transport.sendFindValue(reply,peer.remote,target,DHT.FLAG_NONE,false);reply.await(true);
        require(reply.kind.equals("nodes")||reply.kind.equals("values"),"Actual original get_peers callback differs");
        byte[] token=peer.remote.getRandomID2();require(token!=null&&token.length>0&&token.length<=64,"Missing bounded actual Native token");
        require(peer.remote.getProtocolVersion()==DHTUtilsI2P.PROTOCOL_VERSION_ALT_CONTACTS,"Actual AZ4 reply required for seed flags");
        require(!reply.more&&reply.diversification==DHT.DT_NONE&&reply.nodes.length<=10&&reply.values.length<=128,"Actual original get_peers bounds differ");return reply;
    }
    static Reply store(Peer peer,byte[] target,boolean seed,boolean success)throws Exception{
        // A NONEMPTY value is mandatory: the original treats an empty value as
        // local unannounce success and sends no announce_peer packet.
        DHTTransportValue value=peer.transport.createValue(peer.transport.getLocalContact(),seed?DHT.FLAG_SEEDING:DHT.FLAG_DOWNLOADING,new byte[]{1});
        Reply reply=new Reply(peer.remote);peer.transport.sendStore(reply,peer.remote,new byte[][]{target},new DHTTransportValue[][]{{value}});reply.await(success);
        require(!success||reply.kind.equals("store"),"Actual original raw announce ACK differs");return reply;
    }
    static void exactWriter(Reply reply,Peer writer,boolean seed){
        require(reply.kind.equals("values")&&reply.values.length==1,"Expected exactly one actual announced peer");
        DHTTransportValue value=reply.values[0];
        require(Arrays.equals(value.getValue(),writer.node.info.getHash().getData()),"Actual readback is not writer Destination hash");
        require(value.getFlags()==(seed?DHT.FLAG_SEEDING:DHT.FLAG_DOWNLOADING),"Actual announced seed flag differs");
    }
    static void receipt(int sequence,String command,Reply reply,boolean needsControl)throws Exception{
        require(command.matches("[a-z-]{1,40}"),"Invalid fixed original command");
        json("inbound-original-step-"+sequence+".json","{\"status\":\"ACTUAL_ORIGINAL_HELPER_CALLBACK\",\"sequence\":"+sequence+",\"command\":"+quote(command)+",\"callback\":"+quote(reply.kind)+",\"callbacks\":"+reply.callbacks.get()+",\"failure_class\":"+quote(reply.failure)+",\"values\":"+reply.values.length+",\"nodes\":"+reply.nodes.length+",\"requires_native_control_receipt\":"+needsControl+",\"native_qualification\":false}");
    }
    public static void main(String[] args)throws Exception{
        require(args.length==4&&(args[3].equals("owned-original-inbound-no-grants")||args[3].equals("owned-original-router-eof-no-grants")||args[3].equals("owned-original-receive-expiry-no-grants")),"Explicit original inbound acknowledgement required");
        final boolean routerEofMode=args[3].equals("owned-original-router-eof-no-grants");
        final boolean receiveExpiryOnlyMode=args[3].equals("owned-original-receive-expiry-no-grants");
        int i2cp=DiscoveryRouter.port(args[0]),writerPort=DiscoveryRouter.port(args[1]),readerPort=DiscoveryRouter.port(args[2]);
        NodeInfo nativeNode=nativeNode();Set<Integer> ports=new HashSet<>();
        for(int p:new int[]{nativeNode.getPort(),writerPort,readerPort}){require(p<65535&&ports.add(p)&&ports.add(p+1),"Distinct original query/reply port pairs required");}
        byte[] target=read("inbound-target.hash",20);require(target.length==20,"Exact20-byte inbound target required");
        byte[] downloaderTarget=read("inbound-downloader-target.hash",20);require(downloaderTarget.length==20&&!Arrays.equals(target,downloaderTarget),"Distinct20-byte downloader target required");
        origin(DHTI2P.class,Paths.get("/helper.jar"));origin(DHTTransportI2PRealImpl.class,Paths.get("/helper.jar"));origin(NodeInfo.class,Paths.get("/helper.jar"));origin(DHTTransportReplyHandlerAdapter.class,Paths.get("/core.jar"));
        require(System.getProperty("azureus.config.path","").equals("/work/java-config"),"Owned original core configuration required");
        Class.forName("com.biglybt.core.util.Debug",true,InboundOriginalPeer.class.getClassLoader());
        com.biglybt.core.config.COConfigurationManager.initialise();
        Peer writer=null,reader=null;String stage="original-identities";int sequence=0;boolean pinged=false,found=false,tokenSeen=false,seeded=false,seedRead=false,downloaderToken=false,downloading=false,downloaderRead=false,badToken=false,badTokenRead=false,routerEofFailed=false,receiveExpiryFailed=false,completed=false;
        try{
            writer=new Peer(i2cp,writerPort,"writer",nativeNode);reader=new Peer(i2cp,readerPort,"reader",nativeNode);
            require(!writer.node.info.getHash().equals(reader.node.info.getHash()),"Original identities must differ");
            for(Peer peer:new Peer[]{writer,reader}){String name=peer==writer?"writer":"reader";bytes("inbound-"+name+".node",peer.node.info.getData());bytes("inbound-"+name+".dest",serialize(peer.node.manager.getSession().getMyDestination()));}
            json("inbound-original-ready.json","{\"status\":\"ACTUAL_ORIGINAL_INBOUND_IDENTITIES_READY\",\"identities\":2,\"native_target_hash\":"+quote(hex(target))+",\"downloader_target_hash\":"+quote(hex(downloaderTarget))+",\"receive_expiry_only_mode\":"+receiveExpiryOnlyMode+",\"router_eof_mode\":"+routerEofMode+",\"signed_query_protocol\":17,\"announce_protocol\":18,\"native_qualification\":false}");
            BufferedReader commands=new BufferedReader(new InputStreamReader(System.in,StandardCharsets.US_ASCII));String command;
            commandLoop:while((command=commands.readLine())!=null){
                require(++sequence<=MAX_COMMANDS,"Bounded original inbound command count");require(COMMANDS.contains(command),"Unsupported fixed original inbound command");stage=command;Reply reply;
                if(routerEofMode){require((sequence==1&&command.equals("ping"))||(sequence==2&&command.equals("expect-router-eof"))||(sequence==3&&command.equals("stop")),"Exact bounded original router EOF sequence required");}
                else if(receiveExpiryOnlyMode){require((sequence==1&&command.equals("ping"))||(sequence==2&&command.equals("expect-expired"))||(sequence==3&&command.equals("ping"))||(sequence==4&&command.equals("stop")),"Exact bounded original receive expiry sequence required");}
                else{require(!command.equals("expect-router-eof"),"Explicit original router EOF mode required");}
                switch(command){
                    case "ping":reply=ping(writer,true);pinged=true;break;
                    case "find":
                        reply=new Reply(writer.remote);writer.transport.sendFindNode(reply,writer.remote,target,DHT.FLAG_NONE);reply.await(true);require(reply.kind.equals("nodes")&&reply.nodes.length<=10,"Actual original find_node bounds differ");
                        for(DHTTransportContact contact:reply.nodes){require(contact instanceof DHTTransportContactI2P&&((DHTTransportContactI2P)contact).getNode().getData().length==NodeInfo.LENGTH,"Actual find_node compact contact differs");}found=true;break;
                    case "token":reply=lookup(writer,target);require(reply.values.length==0,"Inbound test key unexpectedly already populated");tokenSeen=true;break;
                    case "seed":require(tokenSeen,"Actual get_peers token required before announce");reply=store(writer,target,true,true);seeded=true;break;
                    case "read-seed":require(seeded,"Real original seed announce required");reply=lookup(reader,target);exactWriter(reply,writer,true);seedRead=true;break;
                    // Original version0 stores preserve the first flags for an
                    // existing target+originator; use a distinct real target.
                    case "downloader-token":require(seedRead,"Actual seed readback required first");reply=lookup(writer,downloaderTarget);require(reply.values.length==0,"Downloader test key unexpectedly populated");downloaderToken=true;break;
                    case "downloader":require(downloaderToken,"Actual downloader token lookup required");reply=store(writer,downloaderTarget,false,true);downloading=true;break;
                    case "read-downloader":require(downloading,"Real original downloader announce required");reply=lookup(reader,downloaderTarget);exactWriter(reply,writer,false);downloaderRead=true;break;
                    case "bad-token":{
                        require(downloaderRead,"Readback baseline required before invalid token control");byte[] saved=reader.remote.getRandomID2();require(saved!=null&&saved.length>0,"Actual token required before invalid token control");byte[] wrong=saved.clone();wrong[0]^=1;
                        // A wrongly accepted new READER originator would add a
                        // second peer, rather than preserve a version0 row.
                        try{reader.remote.setRandomID2(wrong);reply=store(reader,target,true,false);}finally{reader.remote.setRandomID2(saved);}badToken=true;break;
                    }
                    case "read-after-bad-token":require(badToken,"Actual invalid-token attempt required");reply=lookup(reader,target);exactWriter(reply,writer,true);badTokenRead=true;break;
                    // The caller MUST first obtain the corresponding real Native
                    // control receipt. A Java timeout alone proves no cause.
                    case "expect-no-source":case "expect-closed":reply=ping(writer,false);break;
                    case "expect-expired":require(!receiveExpiryOnlyMode||pinged,"Actual positive original ping required before receive expiry");reply=ping(writer,false);receiveExpiryFailed=receiveExpiryOnlyMode;break;
                    case "expect-router-eof":require(routerEofMode&&pinged,"Actual positive original ping required before router EOF failure");reply=ping(writer,false);routerEofFailed=true;break;
                    case "stop":
                        require(routerEofMode?(pinged&&routerEofFailed&&sequence==3):(receiveExpiryOnlyMode?(pinged&&receiveExpiryFailed&&sequence==4):(pinged&&found&&tokenSeen&&seedRead&&downloaderRead&&badTokenRead)),"Original required exchanges incomplete");
                        completed=true;break commandLoop;
                    default:throw new IllegalStateException("Unsupported fixed original inbound command");
                }
                receipt(sequence,command,reply,command.startsWith("expect-"));
            }
            require(completed,"Missing explicit original inbound stop");
        }catch(Throwable error){failure("inbound-original",stage,error);throw error;}
        finally{if(reader!=null)reader.node.close();if(writer!=null)writer.node.close();}
        json("inbound-original-result.json","{\"status\":"+quote(routerEofMode?"ORIGINAL_ROUTER_EOF_EXCHANGES_COMPLETE_NOT_NATIVE_QUALIFICATION":(receiveExpiryOnlyMode?"ORIGINAL_RECEIVE_EXPIRY_EXCHANGES_COMPLETE_NOT_NATIVE_QUALIFICATION":"ORIGINAL_REFERENCE_EXCHANGES_COMPLETE_NOT_NATIVE_QUALIFICATION"))+",\"commands\":"+sequence+",\"identities\":2,\"receive_expiry_only_mode\":"+receiveExpiryOnlyMode+",\"router_eof_mode\":"+routerEofMode+",\"original_close_returned\":true,\"native_qualification\":false,\"source_grants_minted\":0}");
    }
}
