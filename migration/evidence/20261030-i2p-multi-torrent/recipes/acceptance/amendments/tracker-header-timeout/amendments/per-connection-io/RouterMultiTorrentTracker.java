// Test-only bounded multi-torrent tracker/router. No public/default backend.
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

public final class RouterMultiTorrentTracker {
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
      int warmCount=0;boolean sharedChecked=false,leechRetired=false,retired=false;String firstLeechLease=null;BufferedReader commands=new BufferedReader(new InputStreamReader(System.in,StandardCharsets.US_ASCII));String command;
      while((command=commands.readLine())!=null){
        if(command.equals("warm-owned-cache")||command.equals("rewarm-owned-cache")){
          boolean restart=command.equals("rewarm-owned-cache");
          require(restart?warmCount==1&&leechRetired:warmCount==0,"Unexpected accepted-LS2 cache phase");
          Destination[] clients={destination("seed.dest"),destination("leech.dest"),destination("tracker.dest")};
          require(!clients[0].equals(clients[1])&&!clients[0].equals(clients[2])&&!clients[1].equals(clients[2]),"Distinct actual clients required");
          for(Destination client:clients){
            RouterTracker.waitFor(()->ctx.clientManager().isLocal(client.calculateHash())&&ctx.clientNetDb(client.calculateHash())!=null&&ctx.clientNetDb(client.calculateHash()).lookupLeaseSetLocally(client.calculateHash())!=null,30000,"Actual registered client/signed LS2 deadline");
            RouterTracker.typedLocal(ctx,client);
          }
          requireClients(ctx,clients);
          if(restart){final String previous=firstLeechLease;RouterTracker.waitFor(()->freshLease(ctx,clients[1],previous),30000,"Actual new leech signed LS2 must replace its retired lease");}
          LeaseSet[] accepted=new LeaseSet[clients.length];for(int i=0;i<clients.length;i++)accepted[i]=RouterTracker.accepted(ctx,clients[i]);
          String actualLeechLease=leaseHash(accepted[1]);if(!restart)firstLeechLease=actualLeechLease;
          // Only genuine accepted, current, signature-verified Enc4 LS2 objects.
          // No message/status/identity/key/lease is manufactured for lookup.
          for(Destination client:clients){var db=ctx.clientNetDb(client.calculateHash());for(int i=0;i<clients.length;i++){
            db.store(clients[i].calculateHash(),accepted[i]);require(clients[i].equals(db.lookupDestinationLocally(clients[i].calculateHash())),"Public store did not retain accepted Destination");
          }}
          output(restart?"cache-restart-precondition.json":"cache-precondition.json","{\"status\":\""+(restart?"ACTUAL_RESTARTED_THREE_SIGNED_LS2_CACHE_PRECONDITION":"ACTUAL_THREE_SIGNED_LS2_CACHE_PRECONDITION")+"\",\"clients\":3,\"native_clients\":2,\"signature_verified\":true,\"enc4_only\":true,\"zero_hop_pools_checked\":true,\"leech_lease_sha256\":\""+actualLeechLease+"\",\"discovery_or_remote_tunnels_claimed\":false}");warmCount++;
        }else if(command.equals("check-native-shared")){
          require(warmCount==1&&!sharedChecked&&!leechRetired,"Unexpected shared-session checkpoint");
          Destination[] clients={destination("seed.dest"),destination("leech.dest"),destination("tracker.dest")};requireClients(ctx,clients);
          for(Destination client:clients){RouterTracker.typedLocal(ctx,client);RouterTracker.accepted(ctx,client);}
          output("shared-session-precondition.json","{\"status\":\"ACTUAL_SAME_TWO_NATIVE_IDENTITIES_REGISTERED\",\"clients\":3,\"native_clients\":2}");sharedChecked=true;
        }else if(command.equals("leech-closed")){
          require(warmCount==1&&sharedChecked&&!leechRetired&&!retired,"Unexpected partial retirement command");
          Destination leech=destination("leech.dest");RouterTracker.waitFor(()->!ctx.clientManager().isLocal(leech.calculateHash()),10000,"Actual leech I2CP client must retire before restart");
          requireClients(ctx,new Destination[]{destination("seed.dest"),destination("tracker.dest")});
          output("leech-retired.json","{\"status\":\"ACTUAL_LEECH_CLIENT_RETIRED_SEED_PRESERVED\",\"remaining_clients\":2}");leechRetired=true;
        }else if(command.equals("native-closed")){
          require(warmCount==2&&leechRetired&&!retired,"Unexpected native retirement command");Destination seed=destination("seed.dest"),leech=destination("leech.dest");
          RouterTracker.waitFor(()->!ctx.clientManager().isLocal(seed.calculateHash())&&!ctx.clientManager().isLocal(leech.calculateHash()),10000,"Both native actual I2CP TCP clients must retire");
          requireClients(ctx,new Destination[]{destination("tracker.dest")});
          output("native-retired.json","{\"status\":\"ACTUAL_TWO_NATIVE_CLIENTS_RETIRED\",\"clients\":1}");retired=true;
        }else if(command.equals("stop")){
          require(warmCount==2&&sharedChecked&&retired,"Incomplete owned router acceptance");RouterTracker.waitFor(()->ctx.clientManager().listClients().isEmpty(),10000,"Actual tracker I2CP client must retire");
          require(ctx.commSystem().getTransports().isEmpty()&&ctx.commSystem().countActivePeers()==0,"Public transport appeared");
          output("router-result.json","{\"status\":\"PASS\",\"scope\":\"warmed-shared-profile-multi-torrent-transfer-and-restart\",\"public_transports\":0,\"remaining_clients\":0}");return;
        }else throw new IllegalArgumentException("Unknown owned router command");
      }
      throw new EOFException("Owner closed router before acceptance completed");
    }catch(Exception error){error.printStackTrace(System.err);System.err.flush();throw error;}finally{if(router.isAlive())router.shutdown(Router.EXIT_HARD);Runtime.getRuntime().removeShutdownHook(cleanup);}
  }

  static void requireClients(RouterContext ctx,Destination[] clients){
    Set<Destination> expected=new HashSet<>();for(Destination client:clients)expected.add(client);
    require(ctx.clientManager().listClients().equals(expected),"Unexpected actual I2CP client identities/count");
  }
  static String leaseHash(LeaseSet lease)throws Exception{
    ByteArrayOutputStream bytes=new ByteArrayOutputStream();lease.writeBytes(bytes);
    return hex(java.security.MessageDigest.getInstance("SHA-256").digest(bytes.toByteArray()));
  }
  static boolean freshLease(RouterContext ctx,Destination client,String previous){
    try{var db=ctx.clientNetDb(client.calculateHash());if(db==null)return false;
      LeaseSet actual=db.lookupLeaseSetLocally(client.calculateHash());return actual!=null&&!leaseHash(actual).equals(previous);
    }catch(Exception error){throw new IllegalStateException("Cannot inspect actual lease freshness",error);}
  }
  static final class Counts {
    final AtomicInteger seedAnnounces=new AtomicInteger(),leechAnnounces=new AtomicInteger(),requests=new AtomicInteger();
    final AtomicInteger seedReplies=new AtomicInteger(),leechReplies=new AtomicInteger();
    final AtomicInteger seedScrapes=new AtomicInteger(),leechScrapes=new AtomicInteger(),completed=new AtomicInteger();
    boolean seedPresent,leechPresent,leechComplete;
  }
  static Map<String,Counts> torrentTable()throws Exception{
    byte[] bytes;try(InputStream input=Files.newInputStream(RouterTracker.root.resolve("torrent.hashes"))){bytes=input.readNBytes(81);}
    require(bytes.length>=40&&bytes.length<=80&&bytes.length%20==0,"Expected two to four exact full20-byte torrent hashes");
    Map<String,Counts> result=new LinkedHashMap<>();
    for(int offset=0;offset<bytes.length;offset+=20){String hash=hex(Arrays.copyOfRange(bytes,offset,offset+20));
      require(result.put(hash,new Counts())==null,"Duplicate fixture torrent hash");}
    return Collections.unmodifiableMap(result);
  }
  static String countsJson(Map<String,Counts> rows,boolean requireComplete){
    StringBuilder result=new StringBuilder("[");boolean first=true;int index=0;
    for(var item:rows.entrySet()){
      Counts c=item.getValue();if(requireComplete&&index<2)require(c.seedAnnounces.get()>0&&c.leechAnnounces.get()>0&&c.seedReplies.get()>0&&c.leechReplies.get()>0,"Per-hash actual announcements/replies missing");
      if(!first)result.append(',');first=false;index++;
      result.append("{\"info_hash\":\"").append(item.getKey()).append("\",\"seed_announces\":").append(c.seedAnnounces.get())
          .append(",\"leech_announces\":").append(c.leechAnnounces.get()).append(",\"seed_replies\":").append(c.seedReplies.get())
          .append(",\"leech_replies\":").append(c.leechReplies.get()).append(",\"seed_scrapes\":").append(c.seedScrapes.get()).append(",\"leech_scrapes\":").append(c.leechScrapes.get()).append('}');
    }return result.append(']').toString();
  }

  static byte[] readHeader(InputStream input)throws Exception{
    ByteArrayOutputStream out=new ByteArrayOutputStream();int tail=0;
    while(out.size()<8192){int next=input.read();if(next<0)throw new EOFException(out.size()==0?"EOF before actual HTTP request":"EOF within actual HTTP request");require(next==9||next==10||next==13||(next>=32&&next<127),"Non-ASCII actual HTTP header");out.write(next);tail=(tail<<8)|next;if(tail==0x0d0a0d0a)return out.toByteArray();}
    throw new IllegalStateException("Actual HTTP header exceeds8192bytes");
  }
  static String safeFailureMessage(Throwable error){
    String message=error.getMessage();if(message==null)return "Unclassified tracker exception";
    switch(message){
      case "EOF before actual HTTP request":case "EOF within actual HTTP request":
      case "Non-ASCII actual HTTP header":case "Actual HTTP header exceeds8192bytes":
      case "Actual HTTP header shape":case "Expected actual GET tracker request":case "Unknown actual tracker path":
      case "Bounded actual tracker fields":case "Duplicate actual tracker field":
      case "Missing selected actual20byte torrent identity":case "Actual compact announce peer identity required":
      case "Short query escape":case "Bad query escape":case "Non-ASCII query":
      case "Unowned actual tracker peer":case "Actual tracker purpose/ports differ":
      case "Actual accepted tracker connection bound":case "Actual per-hash/total tracker request bound":return message;
      default:return "Unclassified tracker exception";
    }
  }
  static void reportTrackerFailure(String stage,Throwable error){
    String type=error.getClass().getName();if(type.length()>160)type="unknown";String message=safeFailureMessage(error);
    System.err.println("OWNED_TRACKER_APPLICATION_FAILURE stage="+stage+" class="+type+" reason="+message);System.err.flush();
    try{output("tracker-failure.json","{\"status\":\"FAIL_TRACKER_APPLICATION\",\"stage\":\""+stage+"\",\"exception_class\":\""+type+"\",\"message\":\""+message+"\"}");}
    catch(Exception publication){System.err.println("OWNED_TRACKER_FAILURE_PUBLICATION_FAILED");System.err.flush();}
  }
  static byte[] unescape(String value)throws Exception{
    ByteArrayOutputStream out=new ByteArrayOutputStream();for(int i=0;i<value.length();i++){
      char c=value.charAt(i);if(c=='%'){require(i+2<value.length(),"Short query escape");int a=Character.digit(value.charAt(++i),16),b=Character.digit(value.charAt(++i),16);require(a>=0&&b>=0,"Bad query escape");out.write(a*16+b);}
      else{require(c>=33&&c<127,"Non-ASCII query");out.write(c);}
    }return out.toByteArray();
  }
  static final class Request {
    final boolean scrape;final Map<String,byte[]> fields;
    Request(boolean s,Map<String,byte[]> f){scrape=s;fields=f;}
  }
  static Request query(byte[] raw)throws Exception{
    String header=new String(raw,StandardCharsets.US_ASCII);String[] lines=header.split("\\r\\n");require(lines.length>=2&&lines.length<=64,"Actual HTTP header shape");
    String[] first=lines[0].split(" ",-1);require(first.length==3&&first[0].equals("GET")&&(first[2].equals("HTTP/1.0")||first[2].equals("HTTP/1.1")),"Expected actual GET tracker request");
    boolean scrape=first[1].startsWith("/scrape?");require(scrape||first[1].startsWith("/announce?"),"Unknown actual tracker path");
    Map<String,byte[]> result=new HashMap<>();for(String part:first[1].substring(scrape?8:10).split("&",-1)){
      int split=part.indexOf('=');require(split>0&&result.size()<32,"Bounded actual tracker fields");String key=part.substring(0,split);require(key.matches("[A-Za-z0-9_]+")&&result.put(key,unescape(part.substring(split+1)))==null,"Duplicate actual tracker field");
    }
    require(result.containsKey("info_hash")&&result.get("info_hash").length==20,"Missing selected actual20byte torrent identity");
    if(!scrape){require(result.containsKey("peer_id")&&result.get("peer_id").length==20&&Arrays.equals(result.get("compact"),new byte[]{'1'}),"Actual compact announce peer identity required");}
    return new Request(scrape,result);
  }
  static void observeAnnounce(Counts c,boolean isSeed,Map<String,byte[]> fields){
    byte[] left=fields.get("left");require(left!=null&&left.length>0&&left.length<=16,"Actual remaining byte count required");
    String value=new String(left,StandardCharsets.US_ASCII);require(value.matches("[0-9]+"),"Invalid actual remaining count");
    long remaining=Long.parseLong(value);if(isSeed)require(remaining==0,"Actual seeder must have no bytes left");
    byte[] event=fields.get("event");boolean stopped=event!=null&&Arrays.equals(event,"stopped".getBytes(StandardCharsets.US_ASCII));
    if(isSeed){c.seedPresent=!stopped;c.seedAnnounces.incrementAndGet();}
    else{c.leechPresent=!stopped;c.leechComplete=remaining==0;c.leechAnnounces.incrementAndGet();}
    if(event!=null&&Arrays.equals(event,"completed".getBytes(StandardCharsets.US_ASCII)))c.completed.incrementAndGet();
  }
  static byte[] reply(Request request,Counts c,byte[] otherPeer)throws Exception{
    int complete=(c.seedPresent?1:0)+(c.leechPresent&&c.leechComplete?1:0),incomplete=c.leechPresent&&!c.leechComplete?1:0;
    ByteArrayOutputStream out=new ByteArrayOutputStream();
    if(request.scrape){
      out.write("d5:filesd20:".getBytes(StandardCharsets.US_ASCII));out.write(request.fields.get("info_hash"));
      out.write(("d8:completei"+complete+"e10:downloadedi"+c.completed.get()+"e10:incompletei"+incomplete+"eeee").getBytes(StandardCharsets.US_ASCII));
    }else{
      out.write(("d8:completei"+complete+"e10:incompletei"+incomplete+"e8:intervali30e12:min intervali1e5:peers").getBytes(StandardCharsets.US_ASCII));
      out.write(Integer.toString(otherPeer.length).getBytes(StandardCharsets.US_ASCII));out.write(':');out.write(otherPeer);out.write('e');
    }return out.toByteArray();
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
    AtomicInteger requests=new AtomicInteger(),acceptedConnections=new AtomicInteger(),zeroByteEof=new AtomicInteger(),partialHeaderEof=new AtomicInteger(),headerTimeouts=new AtomicInteger(),transportFailures=new AtomicInteger();AtomicReference<Map<String,Counts>> table=new AtomicReference<>();
    try{
      session.connect();require(!session.isClosed()&&session.getMyDestination().equals(tracker),"Actual tracker session not connected");I2PServerSocket server=manager.getServerSocket();server.setSoTimeout(1000);
      Thread application=new Thread(()->{String stage="accept";try{
        while(!stopping.get()){
          stage="accept";I2PSocket accepted;try{accepted=server.accept();}catch(java.net.SocketTimeoutException timeout){continue;}
          boolean isSeed=accepted.getPeerDestination().equals(seed);try(I2PSocket socket=accepted){
            stage="connection-bound";require(acceptedConnections.incrementAndGet()<=256,"Actual accepted tracker connection bound");
            stage="peer-identity";Destination remote=socket.getPeerDestination();require(isSeed||remote.equals(leech),"Unowned actual tracker peer");
            stage="ports";require(socket.getLocalPort()==TRACKER_PORT&&socket.getPort()==(isSeed?seedFrom:leechFrom),"Actual tracker purpose/ports differ");socket.setReadTimeout(15000);
            stage="request-header-query";byte[] header;
            try{header=readHeader(socket.getInputStream());}catch(EOFException cancelled){
              // An owned Pause/Stop/revoke can close an accepted connection.
              // No complete request exists: no hash, announce, scrape or reply credit.
              if("EOF before actual HTTP request".equals(cancelled.getMessage()))zeroByteEof.incrementAndGet();else partialHeaderEof.incrementAndGet();
              output("tracker-eof.json","{\"status\":\"ACTUAL_INCOMPLETE_TRACKER_CONNECTION\",\"accepted_connections\":"+acceptedConnections.get()+",\"zero_byte_eof\":"+zeroByteEof.get()+",\"partial_header_eof\":"+partialHeaderEof.get()+",\"hash_credit\":false,\"reply_credit\":false}");continue;
            }catch(java.net.SocketTimeoutException timeout){
              // No complete request was received within the unchanged15s limit.
              // Retire only this bounded connection, without protocol/hash/reply credit.
              headerTimeouts.incrementAndGet();
              output("tracker-timeouts.json","{\"status\":\"ACTUAL_INCOMPLETE_HEADER_TIMEOUT\",\"accepted_connections\":"+acceptedConnections.get()+",\"header_timeouts\":"+headerTimeouts.get()+",\"hash_credit\":false,\"reply_credit\":false}");continue;
            }
            Request request=query(header);Map<String,byte[]> fields=request.fields;stage="cache-infohash";
            RouterTracker.waitFor(()->Files.exists(RouterTracker.root.resolve("cache-precondition.json")),30000,"Actual native identities/accepted LS2 must precede tracker discovery");
            Map<String,Counts> rows=table.get();if(rows==null){rows=torrentTable();table.set(rows);}
            String hash=hex(fields.get("info_hash"));Counts count=rows.get(hash);require(count!=null,"Unadmitted multi-torrent infohash");
            stage="announce-fields";require(requests.incrementAndGet()<=256&&count.requests.incrementAndGet()<=128,"Actual per-hash/total tracker request bound");
            if(!request.scrape)observeAnnounce(count,isSeed,fields);
            stage="response-write";byte[] body=reply(request,count,(isSeed?leech:seed).calculateHash().getData());OutputStream out=socket.getOutputStream();
            out.write(("HTTP/1.0 200 OK\r\nContent-Type: text/plain\r\nContent-Length: "+body.length+"\r\nConnection: close\r\n\r\n").getBytes(StandardCharsets.US_ASCII));out.write(body);
            stage="response-flush";out.flush();stage="response-close";out.close();
            if(request.scrape){if(isSeed)count.seedScrapes.incrementAndGet();else count.leechScrapes.incrementAndGet();}
            else{if(isSeed)count.seedReplies.incrementAndGet();else count.leechReplies.incrementAndGet();}
            output("tracker-progress.json","{\"status\":\"ACTUAL_MULTI_HASH_TRACKER_OBSERVATIONS\",\"hashes\":"+countsJson(rows,false)+"}");
          }catch(IOException disconnected){
            // A transport failure belongs to this owned connection. Complete
            // parsed requests retain only their actual observations; no reply
            // is credited unless the real write/flush/close already completed.
            transportFailures.incrementAndGet();
            output("tracker-transport.json","{\"status\":\"ACTUAL_TRACKER_CONNECTION_IO_FAILURE\",\"stage\":\""+stage+"\",\"transport_failures\":"+transportFailures.get()+",\"new_reply_credit\":false}");
          }
        }
      }catch(Throwable error){if(!stopping.get()&&failure.compareAndSet(null,error)){reportTrackerFailure(stage,error);}}},"owned-original-http-tracker");application.setDaemon(true);application.start();
      output("tracker-ready.json","{\"status\":\"ACTUAL_EXTERNAL_TORRENT_TRACKER_CONNECTED\",\"tracker_destination_sha256\":\""+hex(tracker.calculateHash().getData())+"\",\"seed_destination_sha256\":\""+hex(seed.calculateHash().getData())+"\",\"leech_destination_sha256\":\""+hex(leech.calculateHash().getData())+"\",\"tracker_port\":80,\"seed_from_port\":"+seedFrom+",\"leech_from_port\":"+leechFrom+"}");
      BufferedReader commands=new BufferedReader(new InputStreamReader(System.in,StandardCharsets.US_ASCII));String command;
      while((command=commands.readLine())!=null){
        {require(command.equals("stop"),"Unknown actual tracker owner command");
          if(failure.get()!=null)throw new IllegalStateException("Actual tracker application failed",failure.get());
          Map<String,Counts> rows=table.get();require(rows!=null,"Actual multi-hash table was never observed");String actualCounts=countsJson(rows,true);
          stopping.set(true);manager.destroySocketManager();application.join(5000);require(!application.isAlive()&&session.isClosed(),"Tracker application/session not retired");
          output("tracker-result.json","{\"status\":\"PASS\",\"hashes\":"+actualCounts+",\"requests\":"+requests.get()+",\"accepted_connections\":"+acceptedConnections.get()+",\"zero_byte_eof\":"+zeroByteEof.get()+",\"partial_header_eof\":"+partialHeaderEof.get()+",\"header_timeouts\":"+headerTimeouts.get()+",\"transport_failures\":"+transportFailures.get()+",\"external_i2cp\":true,\"session_closed\":true,\"late_reply_qualified\":false}");return;
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
