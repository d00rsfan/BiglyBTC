// Test-only real modified-core tracker/router. Never a public/default backend.
// Uses the unchanged qualified RouterTracker utility and identical router policy.
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.nio.file.attribute.PosixFilePermissions;
import java.util.*;
import java.util.concurrent.atomic.*;
import net.i2p.I2PAppContext;
import net.i2p.client.*;
import net.i2p.client.streaming.*;
import net.i2p.client.streaming.impl.I2PSocketManagerFull;
import net.i2p.crypto.SigType;
import net.i2p.data.*;
import net.i2p.router.*;

public final class RouterTorrentTracker {
  static final int TRACKER_PORT=80;
  static void require(boolean ok,String why){RouterTracker.require(ok,why);}
  static Destination destination(String leaf)throws Exception{return RouterTracker.destination(leaf);}
  static void output(String leaf,String json)throws Exception{RouterTracker.output(leaf,json);}
  static String hex(byte[] bytes){return RouterTracker.hex(bytes);}

  static void router(int port)throws Exception{
    Path data=RouterTracker.root.resolve("router");Files.createDirectory(data);
    Files.setPosixFilePermissions(data,PosixFilePermissions.fromString("rwx------"));
    Files.createDirectory(data.resolve("clients.config.d"));Files.write(data.resolve("clients.config"),new byte[0],StandardOpenOption.CREATE_NEW);
    Properties p=new Properties();
    for(String name:new String[]{"base","config","router","log","pid"})p.setProperty("i2p.dir."+name,data.toString());
    p.setProperty("router.configLocation",data.resolve("router.config").toString());p.setProperty("router.pingFile",data.resolve("router.ping").toString());p.setProperty("router.clientConfigFile",data.resolve("clients.config").toString());
    p.setProperty("i2cp.hostname","127.0.0.1");p.setProperty("i2cp.port",Integer.toString(port));p.setProperty("i2cp.tcp.bindAllInterfaces","false");p.setProperty("i2cp.disableInterface","false");p.setProperty("i2cp.disableLoopback","false");
    p.setProperty("router.reseedDisable","true");p.setProperty("i2np.udp.enable","false");p.setProperty("i2np.ntcp.enable","false");p.setProperty("i2np.upnp.enable","false");p.setProperty("i2np.upnp.ipv6.enable","false");p.setProperty("time.disabled","true");p.setProperty("time.disableOffset","true");p.setProperty("router.floodfillParticipant","false");p.setProperty("router.sharePercentage","0");
    for(String side:new String[]{"router.inboundPool.","router.outboundPool."}){
      p.setProperty(side+"length","0");p.setProperty(side+"lengthVariance","0");p.setProperty(side+"quantity","1");p.setProperty(side+"backupQuantity","0");p.setProperty(side+"allowZeroHop","true");
    }
    try(OutputStream out=Files.newOutputStream(data.resolve("router.config"),StandardOpenOption.CREATE_NEW)){p.store(out,"Owned isolated provider-transfer acceptance only");}
    Router router=new Router(p);router.setKillVMOnEnd(false);RouterContext ctx=router.getContext();
    Thread cleanup=new Thread(()->{if(router.isAlive())router.shutdown(Router.EXIT_HARD);},"owned-transfer-router-cleanup");Runtime.getRuntime().addShutdownHook(cleanup);
    try{
      router.runRouter();RouterTracker.waitFor(()->ctx.clientManager().isAlive()&&ctx.netDb().isInitialized(),60000,"Owned router startup deadline");
      require(ctx.commSystem().getTransports().isEmpty()&&ctx.commSystem().countActivePeers()==0,"Unexpected public transport");
      output("router-ready.json","{\"status\":\"ACTUAL_ROUTER_STARTED\",\"port\":"+port+",\"active_public_transports\":0}");
      boolean warmed=false,retired=false;BufferedReader commands=new BufferedReader(new InputStreamReader(System.in,StandardCharsets.US_ASCII));String command;
      while((command=commands.readLine())!=null){
        if(command.equals("warm-owned-cache")){
          require(!warmed,"Only one explicit accepted-LS2 cache precondition");
          Destination[] clients={destination("seed.dest"),destination("leech.dest"),destination("tracker.dest")};
          require(!clients[0].equals(clients[1])&&!clients[0].equals(clients[2])&&!clients[1].equals(clients[2]),"Distinct actual clients required");
          for(Destination client:clients){
            RouterTracker.waitFor(()->ctx.clientManager().isLocal(client.calculateHash())&&ctx.clientNetDb(client.calculateHash())!=null&&ctx.clientNetDb(client.calculateHash()).lookupLeaseSetLocally(client.calculateHash())!=null,30000,"Actual registered client/signed LS2 deadline");
            RouterTracker.typedLocal(ctx,client);
          }
          LeaseSet[] accepted=new LeaseSet[clients.length];for(int i=0;i<clients.length;i++)accepted[i]=RouterTracker.accepted(ctx,clients[i]);
          // Only genuine accepted, current, signature-verified Enc4 LS2 objects.
          // No message/status/identity/key/lease is manufactured for lookup.
          for(Destination client:clients){var db=ctx.clientNetDb(client.calculateHash());for(int i=0;i<clients.length;i++){
            db.store(clients[i].calculateHash(),accepted[i]);require(clients[i].equals(db.lookupDestinationLocally(clients[i].calculateHash())),"Public store did not retain accepted Destination");
          }}
          output("cache-precondition.json","{\"status\":\"ACTUAL_THREE_SIGNED_LS2_CACHE_PRECONDITION\",\"clients\":3,\"native_clients\":2,\"signature_verified\":true,\"enc4_only\":true,\"zero_hop_pools_checked\":true,\"discovery_or_remote_tunnels_claimed\":false}");warmed=true;
        }else if(command.equals("native-closed")){
          require(warmed&&!retired,"Unexpected native retirement command");Destination seed=destination("seed.dest"),leech=destination("leech.dest");
          RouterTracker.waitFor(()->!ctx.clientManager().isLocal(seed.calculateHash())&&!ctx.clientManager().isLocal(leech.calculateHash()),10000,"Both native actual I2CP TCP clients must retire");
          output("native-retired.json","{\"status\":\"ACTUAL_TWO_NATIVE_CLIENTS_RETIRED\",\"clients\":2}");retired=true;
        }else if(command.equals("stop")){
          require(warmed&&retired,"Incomplete owned router acceptance");RouterTracker.waitFor(()->ctx.clientManager().listClients().isEmpty(),10000,"Actual tracker I2CP client must retire");
          require(ctx.commSystem().getTransports().isEmpty()&&ctx.commSystem().countActivePeers()==0,"Public transport appeared");
          output("router-result.json","{\"status\":\"PASS\",\"scope\":\"warmed-three-local-clients-real-provider-transfer\",\"public_transports\":0,\"remaining_clients\":0}");return;
        }else throw new IllegalArgumentException("Unknown owned router command");
      }
      throw new EOFException("Owner closed router before acceptance completed");
    }finally{if(router.isAlive())router.shutdown(Router.EXIT_HARD);Runtime.getRuntime().removeShutdownHook(cleanup);}
  }

  static byte[] readHeader(InputStream input)throws Exception{
    ByteArrayOutputStream out=new ByteArrayOutputStream();int tail=0;
    while(out.size()<8192){int next=input.read();require(next>=0,"EOF in actual HTTP request");require(next==9||next==10||next==13||(next>=32&&next<127),"Non-ASCII actual HTTP header");out.write(next);tail=(tail<<8)|next;if(tail==0x0d0a0d0a)return out.toByteArray();}
    throw new IllegalStateException("Actual HTTP header exceeds8192bytes");
  }
  static byte[] unescape(String value)throws Exception{
    ByteArrayOutputStream out=new ByteArrayOutputStream();for(int i=0;i<value.length();i++){
      char c=value.charAt(i);if(c=='%'){require(i+2<value.length(),"Short query escape");int a=Character.digit(value.charAt(++i),16),b=Character.digit(value.charAt(++i),16);require(a>=0&&b>=0,"Bad query escape");out.write(a*16+b);}
      else{require(c>=33&&c<127,"Non-ASCII query");out.write(c);}
    }return out.toByteArray();
  }
  static Map<String,byte[]> query(byte[] raw)throws Exception{
    String header=new String(raw,StandardCharsets.US_ASCII);String[] lines=header.split("\r\n");require(lines.length>=2&&lines.length<=64,"Actual HTTP header shape");
    String[] first=lines[0].split(" ",-1);require(first.length==3&&first[0].equals("GET")&&(first[2].equals("HTTP/1.0")||first[2].equals("HTTP/1.1"))&&first[1].startsWith("/announce?"),"Expected actual GET announce");
    Map<String,byte[]> result=new HashMap<>();for(String part:first[1].substring(10).split("&",-1)){
      int split=part.indexOf('=');require(split>0&&result.size()<32,"Bounded actual announce fields");String key=part.substring(0,split);require(key.matches("[A-Za-z0-9_]+")&&result.put(key,unescape(part.substring(split+1)))==null,"Duplicate actual announce field");
    }
    require(result.containsKey("info_hash")&&result.get("info_hash").length==20&&result.containsKey("peer_id")&&result.get("peer_id").length==20,"Missing actual20byte torrent/peer identity");
    require(Arrays.equals(result.get("compact"),new byte[]{'1'}),"Actual I2P compact announce required");return result;
  }
  static byte[] reply(byte[] seedHash)throws Exception{
    ByteArrayOutputStream out=new ByteArrayOutputStream();out.write("d8:intervali30e12:min intervali1e5:peers".getBytes(StandardCharsets.US_ASCII));
    out.write(Integer.toString(seedHash.length).getBytes(StandardCharsets.US_ASCII));out.write(':');out.write(seedHash);out.write('e');return out.toByteArray();
  }
  static void tracker(int port,int seedFrom,int leechFrom)throws Exception{
    require(!I2PAppContext.getGlobalContext().isRouterContext(),"Tracker must use actual external I2CP TCP");
    for(Class<?> cl:new Class<?>[]{I2PSocketManagerFull.class,I2PSocket.class,I2PSession.class})RouterTracker.origin(cl);
    I2PClient client=I2PClientFactory.createClient();ByteArrayOutputStream seedKeys=new ByteArrayOutputStream(),leechKeys=new ByteArrayOutputStream(),trackerKeys=new ByteArrayOutputStream();
    Destination seed=client.createDestination(seedKeys,SigType.EdDSA_SHA512_Ed25519),leech=client.createDestination(leechKeys,SigType.EdDSA_SHA512_Ed25519),tracker=client.createDestination(trackerKeys,SigType.EdDSA_SHA512_Ed25519);
    RouterTracker.bytes("seed.keys",seedKeys.toByteArray());RouterTracker.bytes("leech.keys",leechKeys.toByteArray());RouterTracker.bytes("seed.dest",RouterTracker.serialize(seed));RouterTracker.bytes("leech.dest",RouterTracker.serialize(leech));RouterTracker.bytes("tracker.dest",RouterTracker.serialize(tracker));
    RouterTracker.origin(Base32.class);RouterTracker.bytes("tracker.url",("http://"+Base32.encode(tracker.calculateHash().getData())+".b32.i2p/announce").getBytes(StandardCharsets.US_ASCII));
    Properties p=RouterTracker.zeroHop();p.setProperty(I2PClient.PROP_TCP_HOST,"127.0.0.1");p.setProperty(I2PClient.PROP_TCP_PORT,Integer.toString(port));
    p.setProperty("i2p.streaming.enforceProtocol","true");p.setProperty("i2p.streaming.answerPings","false");p.setProperty("i2p.streaming.maxMessageSize","1730");p.setProperty("i2p.streaming.initialWindowSize","3");p.setProperty("i2p.streaming.maxWindowSize","8");p.setProperty("i2p.streaming.readTimeout","15000");p.setProperty("i2p.streaming.writeTimeout","15000");p.setProperty("i2p.streaming.inactivityTimeout","60000");
    I2PSession session=client.createSession(new ByteArrayInputStream(trackerKeys.toByteArray()),p);
    I2PSocketManagerFull manager=new I2PSocketManagerFull(I2PAppContext.getGlobalContext(),session,p,"OwnedProviderTorrentTracker",d->d.equals(seed)||d.equals(leech));
    AtomicReference<Throwable> failure=new AtomicReference<>();AtomicBoolean stopping=new AtomicBoolean();
    AtomicInteger seedAnnounces=new AtomicInteger(),leechAnnounces=new AtomicInteger(),seedReplies=new AtomicInteger(),leechReplies=new AtomicInteger(),withdrawnConnections=new AtomicInteger();
    try{
      session.connect();require(!session.isClosed()&&session.getMyDestination().equals(tracker),"Actual tracker session not connected");I2PServerSocket server=manager.getServerSocket();server.setSoTimeout(1000);
      Thread application=new Thread(()->{String stage="accept";try{
        while(!stopping.get()){
          stage="accept";I2PSocket accepted;try{accepted=server.accept();}catch(java.net.SocketTimeoutException timeout){continue;}
          boolean isSeed=accepted.getPeerDestination().equals(seed);try(I2PSocket socket=accepted){
            stage="peer-identity";Destination remote=socket.getPeerDestination();require(isSeed||remote.equals(leech),"Unowned actual tracker peer");
            stage="ports";require(socket.getLocalPort()==TRACKER_PORT&&socket.getPort()==(isSeed?seedFrom:leechFrom),"Actual tracker purpose/ports differ");socket.setReadTimeout(15000);
            stage="request-header-query";Map<String,byte[]> fields=query(readHeader(socket.getInputStream()));stage="cache-infohash";RouterTracker.waitFor(()->Files.exists(RouterTracker.root.resolve("cache-precondition.json")),30000,"Both actual native identities/accepted LS2 must precede tracker discovery");byte[] hash;try(InputStream expected=Files.newInputStream(RouterTracker.root.resolve("torrent.infohash"))){hash=expected.readNBytes(21);}require(hash.length==20&&Arrays.equals(hash,fields.get("info_hash")),"Wrong actual torrent infohash");
            stage="announce-fields";require(seedAnnounces.get()+leechAnnounces.get()<64,"Actual tracker request bound");
            if(isSeed){require(Arrays.equals(fields.get("left"),new byte[]{'0'}),"Actual seeder must have no bytes left");if(seedAnnounces.incrementAndGet()==1)output("seeder-announced.json","{\"status\":\"ACTUAL_SEEDER_ANNOUNCED\",\"info_hash\":\""+hex(hash)+"\"}");}
            else{leechAnnounces.incrementAndGet();}
            stage="response-write";byte[] body=reply((isSeed?leech:seed).calculateHash().getData());OutputStream out=socket.getOutputStream();
            out.write(("HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nContent-Length: "+body.length+"\r\nConnection: close\r\n\r\n").getBytes(StandardCharsets.US_ASCII));out.write(body);stage="response-flush";out.flush();stage="response-close";out.close();if(isSeed)seedReplies.incrementAndGet();else leechReplies.incrementAndGet();
          }catch(IOException closed){
            // The native subject separately proves completed transfer and real
            // key withdrawal. Only a leech IO close after its explicit source
            // replacement marker is expected; parser/shape/identity failures
            // and healthy writes remain fatal. No late-reply credit is given.
            if(!isSeed&&Files.exists(RouterTracker.root.resolve("source-replacement-started.json")))withdrawnConnections.incrementAndGet();else throw closed;
          }
        }
      }catch(Throwable error){if(!stopping.get()&&failure.compareAndSet(null,error)){System.err.println("OWNED_TRACKER_APPLICATION_FAILURE stage="+stage+" class="+error.getClass().getName());}}},"owned-original-http-tracker");application.setDaemon(true);application.start();
      output("tracker-ready.json","{\"status\":\"ACTUAL_EXTERNAL_TORRENT_TRACKER_CONNECTED\",\"tracker_destination_sha256\":\""+hex(tracker.calculateHash().getData())+"\",\"seed_destination_sha256\":\""+hex(seed.calculateHash().getData())+"\",\"leech_destination_sha256\":\""+hex(leech.calculateHash().getData())+"\",\"tracker_port\":80,\"seed_from_port\":"+seedFrom+",\"leech_from_port\":"+leechFrom+"}");
      BufferedReader commands=new BufferedReader(new InputStreamReader(System.in,StandardCharsets.US_ASCII));String command;
      while((command=commands.readLine())!=null){
        {require(command.equals("stop"),"Unknown actual tracker owner command");
          if(failure.get()!=null)throw new IllegalStateException("Actual tracker application failed",failure.get());
          require(seedAnnounces.get()>0&&leechAnnounces.get()>0&&seedReplies.get()>0&&leechReplies.get()>0,"Actual two-sided announcements/replies missing");
          stopping.set(true);manager.destroySocketManager();application.join(5000);require(!application.isAlive()&&session.isClosed(),"Tracker application/session not retired");
          output("tracker-result.json","{\"status\":\"PASS\",\"seed_announces\":"+seedAnnounces.get()+",\"leech_announces\":"+leechAnnounces.get()+",\"seed_replies\":"+seedReplies.get()+",\"leech_replies\":"+leechReplies.get()+",\"http_replies\":"+(seedReplies.get()+leechReplies.get())+",\"withdrawn_connections\":"+withdrawnConnections.get()+",\"late_reply_qualified\":false,\"external_i2cp\":true,\"application_transfer_claimed\":false,\"session_closed\":true}");return;
        }
      }
      throw new EOFException("Owner closed tracker before completion");
    }finally{stopping.set(true);manager.destroySocketManager();if(!session.isClosed())session.destroySession();}
  }

  public static void main(String[] args){int exit=1;try{
    require(args.length==5,"router|tracker /work RANDOM_I2CP_PORT SEED_FROM_PORT LEECH_FROM_PORT");RouterTracker.root=Paths.get(args[1]);require(RouterTracker.root.equals(Paths.get("/work")),"Only fenced owned /work accepted");
    int port=Integer.parseInt(args[2]),seedFrom=Integer.parseInt(args[3]),leechFrom=Integer.parseInt(args[4]);require(port>=1024&&port<=65535&&seedFrom>=0&&seedFrom<=65535&&leechFrom>=0&&leechFrom<=65535,"Bounded explicit fixture ports required");RouterTracker.origins();
    if(args[0].equals("router"))router(port);else{require(args[0].equals("tracker"),"Unknown subject");tracker(port,seedFrom,leechFrom);}exit=0;
  }catch(Throwable error){error.printStackTrace(System.err);}finally{System.exit(exit);}}
}
