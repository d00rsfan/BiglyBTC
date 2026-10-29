// Test-only real retained modified-core router and tracker. No public backend.
import java.io.*;
import java.net.*;
import java.nio.*;
import java.nio.file.*;
import java.nio.file.attribute.PosixFilePermissions;
import java.util.*;
import java.util.concurrent.atomic.AtomicReference;
import java.util.function.BooleanSupplier;
import net.i2p.I2PAppContext;
import net.i2p.client.*;
import net.i2p.client.datagram.*;
import net.i2p.crypto.SigType;
import net.i2p.data.*;
import net.i2p.router.*;
import net.i2p.router.tunnel.pool.TunnelPool;

public final class RouterTracker {
  static Path root;
  static void require(boolean ok,String why){if(!ok)throw new IllegalStateException(why);}
  static String hex(byte[] b){StringBuilder out=new StringBuilder(2*b.length);for(byte x:b)out.append(String.format(Locale.ROOT,"%02x",x&255));return out.toString();}
  static void output(String leaf,String json)throws Exception{
    Path pending=root.resolve(leaf+".tmp");Files.write(pending,(json+"\n").getBytes(java.nio.charset.StandardCharsets.UTF_8),StandardOpenOption.CREATE_NEW);
    Files.setPosixFilePermissions(pending,PosixFilePermissions.fromString("rw-------"));Files.move(pending,root.resolve(leaf),StandardCopyOption.ATOMIC_MOVE);
  }
  static void bytes(String leaf,byte[] bytes)throws Exception{
    require(bytes.length<=1024,"Unbounded key/Destination fixture");Path p=root.resolve(leaf);Files.write(p,bytes,StandardOpenOption.CREATE_NEW);Files.setPosixFilePermissions(p,PosixFilePermissions.fromString("rw-------"));
  }
  static Destination destination(String leaf)throws Exception{
    byte[] b=Files.readAllBytes(root.resolve(leaf));require(b.length<=1024,"Unbounded owned Destination");ByteArrayInputStream in=new ByteArrayInputStream(b);Destination d=Destination.create(in);require(in.available()==0,"Trailing owned Destination");return d;
  }
  static byte[] serialize(Destination d)throws Exception{ByteArrayOutputStream b=new ByteArrayOutputStream();d.writeBytes(b);return b.toByteArray();}
  static void waitFor(BooleanSupplier condition,int milliseconds,String reason)throws Exception{
    long end=System.nanoTime()+milliseconds*1000000L;while(!condition.getAsBoolean()){require(System.nanoTime()<end,reason);Thread.sleep(25);}
  }
  static void origin(Class<?> cl)throws Exception{require(cl.getProtectionDomain().getCodeSource().getLocation().toURI().equals(new File("/reference.jar").toURI()),"Unexpected class origin: "+cl.getName());}
  static void origins()throws Exception{for(Class<?> c:new Class<?>[]{Router.class,I2PClientFactory.class,I2PSession.class,Datagram2.class,Datagram3.class,Destination.class,LeaseSet2.class})origin(c);}
  static Properties zeroHop(){
    Properties p=new Properties();for(String side:new String[]{"inbound.","outbound."}){
      p.setProperty(side+"length","0");p.setProperty(side+"lengthVariance","0");p.setProperty(side+"quantity","1");p.setProperty(side+"backupQuantity","0");p.setProperty(side+"allowZeroHop","true");
    }
    p.setProperty("i2cp.leaseSetType","3");p.setProperty("i2cp.leaseSetEncType","0");p.setProperty("i2cp.dontPublishLeaseSet","false");p.setProperty("i2cp.fastReceive","true");p.setProperty("i2cp.messageReliability","none");return p;
  }
  static void typedLocal(RouterContext ctx,Destination d){
    Hash hash=d.calculateHash();require(ctx.clientManager().isLocal(hash),"Expected actual local registered client");
    require(ctx.clientManager().getClientSessionConfig(d)!=null,"Missing actual signed SessionConfig");
    for(boolean inbound:new boolean[]{true,false}){
      TunnelPool pool=inbound?ctx.tunnelManager().getInboundPool(hash):ctx.tunnelManager().getOutboundPool(hash);
      require(pool!=null&&pool.getSettings().getLength()==0&&pool.getSettings().getLengthVariance()==0,"Wrong actual zero-hop pool policy");
      require(!pool.listTunnels().isEmpty(),"No actual zero-hop tunnel");
      for(TunnelInfo t:pool.listTunnels())require(t.getLength()==1&&t.getPeer(0).equals(ctx.routerHash()),"Nonlocal hop in owned router fixture");
    }
  }
  static LeaseSet accepted(RouterContext ctx,Destination d){
    var db=ctx.clientNetDb(d.calculateHash());require(db!=null,"Missing actual client subDB");LeaseSet ls=db.lookupLeaseSetLocally(d.calculateHash());
    require(ls!=null&&ls.getType()==3&&ls.getDestination().equals(d)&&ls.verifySignature()&&ls.getLeaseCount()>0&&ls.getLatestLeaseDate()>ctx.clock().now(),"No actual current accepted signed LS2");return ls;
  }
  static void router(int port)throws Exception{
    Path data=root.resolve("router");Files.createDirectory(data);Files.setPosixFilePermissions(data,PosixFilePermissions.fromString("rwx------"));
    Files.createDirectory(data.resolve("clients.config.d"));Files.write(data.resolve("clients.config"),new byte[0],StandardOpenOption.CREATE_NEW);
    Properties p=new Properties();p.setProperty("i2p.dir.base",data.toString());p.setProperty("i2p.dir.config",data.toString());p.setProperty("i2p.dir.router",data.toString());p.setProperty("i2p.dir.log",data.toString());p.setProperty("i2p.dir.pid",data.toString());
    p.setProperty("router.configLocation",data.resolve("router.config").toString());p.setProperty("router.pingFile",data.resolve("router.ping").toString());p.setProperty("router.clientConfigFile",data.resolve("clients.config").toString());
    p.setProperty("i2cp.hostname","127.0.0.1");p.setProperty("i2cp.port",Integer.toString(port));p.setProperty("i2cp.tcp.bindAllInterfaces","false");p.setProperty("i2cp.disableInterface","false");p.setProperty("i2cp.disableLoopback","false");
    p.setProperty("router.reseedDisable","true");p.setProperty("i2np.udp.enable","false");p.setProperty("i2np.ntcp.enable","false");p.setProperty("i2np.upnp.enable","false");p.setProperty("i2np.upnp.ipv6.enable","false");p.setProperty("time.disabled","true");p.setProperty("time.disableOffset","true");p.setProperty("router.floodfillParticipant","false");p.setProperty("router.sharePercentage","0");
    for(String side:new String[]{"router.inboundPool.","router.outboundPool."}){p.setProperty(side+"length","0");p.setProperty(side+"lengthVariance","0");p.setProperty(side+"quantity","1");p.setProperty(side+"backupQuantity","0");p.setProperty(side+"allowZeroHop","true");}
    try(OutputStream out=Files.newOutputStream(data.resolve("router.config"),StandardOpenOption.CREATE_NEW)){p.store(out,"Owned isolated zero-hop acceptance only");}
    Router router=new Router(p);router.setKillVMOnEnd(false);final RouterContext ctx=router.getContext();
    Thread cleanup=new Thread(()->{if(router.isAlive())router.shutdown(Router.EXIT_HARD);},"owned-router-cleanup");Runtime.getRuntime().addShutdownHook(cleanup);
    try{
      router.runRouter();waitFor(()->ctx.clientManager().isAlive()&&ctx.netDb().isInitialized(),60000,"Router startup deadline");
      require(ctx.commSystem().getTransports().isEmpty()&&ctx.commSystem().countActivePeers()==0,"Unexpected actual public transport");
      output("router-ready.json","{\"status\":\"ACTUAL_ROUTER_STARTED\",\"port\":"+port+",\"active_public_transports\":0}");
      BufferedReader commands=new BufferedReader(new InputStreamReader(System.in,java.nio.charset.StandardCharsets.US_ASCII));String command;boolean warmed=false,retired=false;
      while((command=commands.readLine())!=null){
        if(command.equals("warm-owned-cache")){
          require(!warmed,"Cache precondition may be established only once");Destination tracker=destination("tracker.dest"),nativeDest=destination("native.dest");
          waitFor(()->ctx.clientManager().isLocal(nativeDest.calculateHash())&&ctx.clientNetDb(nativeDest.calculateHash()).lookupLeaseSetLocally(nativeDest.calculateHash())!=null&&ctx.clientNetDb(tracker.calculateHash()).lookupLeaseSetLocally(tracker.calculateHash())!=null,30000,"Native and tracker actual accepted LeaseSet deadline");
          typedLocal(ctx,tracker);typedLocal(ctx,nativeDest);LeaseSet ls=accepted(ctx,tracker);accepted(ctx,nativeDest);
          require(!tracker.equals(nativeDest),"Distinct local Destinations required");
          // Explicit fixture precondition: actual accepted+verified tracker LS2
          // is copied via PUBLIC router store into this owned client's subDB.
          // No I2CP response, lease, status, key, or count is fabricated here.
          var db=ctx.clientNetDb(nativeDest.calculateHash());db.store(tracker.calculateHash(),ls);
          Destination found=db.lookupDestinationLocally(tracker.calculateHash());require(tracker.equals(found),"Owned signed cache precondition failed");
          ByteArrayOutputStream wire=new ByteArrayOutputStream();ls.writeBytes(wire);
          output("cache-precondition.json","{\"status\":\"ACTUAL_SIGNED_LS2_CACHE_PRECONDITION\",\"native_destination_sha256\":\""+hex(nativeDest.calculateHash().getData())+"\",\"tracker_destination_sha256\":\""+hex(tracker.calculateHash().getData())+"\",\"signed_ls2_sha256\":\""+hex(java.security.MessageDigest.getInstance("SHA-256").digest(wire.toByteArray()))+"\",\"signature_verified\":true,\"zero_hop_pools_checked\":true}");warmed=true;
        }else if(command.equals("native-closed")){
          require(warmed&&!retired,"Unexpected retirement command");Destination nativeDest=destination("native.dest");waitFor(()->!ctx.clientManager().isLocal(nativeDest.calculateHash()),10000,"Native actual TCP retirement deadline");
          output("native-retired.json","{\"status\":\"ACTUAL_NATIVE_CLIENT_RETIRED\"}");retired=true;
        }else if(command.equals("stop")){
          require(warmed&&retired,"Cannot credit incomplete router acceptance");waitFor(()->ctx.clientManager().listClients().isEmpty(),10000,"Tracker actual client retirement deadline");require(ctx.commSystem().getTransports().isEmpty()&&ctx.commSystem().countActivePeers()==0,"Public transport appeared");
          output("router-result.json","{\"status\":\"PASS\",\"scope\":\"warmed-local-cache-and-real-local-client-forwarding\",\"public_transports\":0,\"remaining_clients\":0}");return;
        }else throw new IllegalArgumentException("Unknown owned controller command");
      }
      throw new EOFException("Owner closed controller before complete acceptance");
    }finally{if(router.isAlive())router.shutdown(Router.EXIT_HARD);Runtime.getRuntime().removeShutdownHook(cleanup);}
  }
  static final class Tracker implements I2PSessionMuxedListener {
    final I2PSession session;final Destination nativeDest;final long connection=new java.security.SecureRandom().nextLong();
    final AtomicReference<Throwable> failure=new AtomicReference<>();int connects,scrapes,replies;int connectTransaction;volatile boolean stopping;
    Tracker(I2PSession session,Destination nativeDest){this.session=session;this.nativeDest=nativeDest;}
    synchronized void receive(int message,int protocol,int from,int to)throws Exception{
      require(from==9000&&to==6969,"Wrong actual tracker ports");byte[] data=session.receiveMessage(message);require(data!=null&&data.length<=1024,"Wrong actual tracker datagram size");byte[] payload;
      if(protocol==19){Datagram2 d=Datagram2.load(I2PAppContext.getGlobalContext(),session,data);require(d.getSender().equals(nativeDest),"Wrong actual signed connect sender");payload=d.getPayload();}
      else{require(protocol==20,"Unexpected actual datagram protocol");Datagram3 d=Datagram3.load(I2PAppContext.getGlobalContext(),session,data);require(d.getSender().equals(nativeDest.calculateHash()),"Wrong claimed scrape sender");payload=d.getPayload();}
      ByteBuffer in=ByteBuffer.wrap(payload).order(ByteOrder.BIG_ENDIAN);byte[] reply;
      if(protocol==19){require(connects==0&&scrapes==0&&payload.length==16&&in.getLong()==0x41727101980L&&in.getInt()==0,"Wrong actual BEP15 connect");connectTransaction=in.getInt();connects++;reply=ByteBuffer.allocate(16).putInt(0).putInt(connectTransaction).putLong(connection).array();}
      else{require(connects==1&&scrapes==0&&payload.length==36&&in.getLong()==connection&&in.getInt()==2,"Wrong actual BEP15 scrape connection");int tx=in.getInt();require(tx!=connectTransaction,"Reused tracker transaction");for(int i=0;i<20;i++)require(in.get()==0x51,"Wrong actual scrape hash");scrapes++;reply=ByteBuffer.allocate(20).putInt(2).putInt(tx).putInt(3).putInt(4).putInt(5).array();}
      SendMessageOptions options=new SendMessageOptions();options.setDate(System.currentTimeMillis()+10000);options.setSendLeaseSet(false);
      require(session.sendMessage(nativeDest,reply,0,reply.length,18,6969,9000,options),"Original Java tracker could not enqueue RAW18 reply");replies++;
    }
    public void messageAvailable(I2PSession s,int id,long size,int protocol,int from,int to){try{require(s==session&&size<=1024,"Wrong actual tracker message");receive(id,protocol,from,to);}catch(Throwable e){failure.compareAndSet(null,e);}}
    public void messageAvailable(I2PSession s,int id,long size){failure.compareAndSet(null,new IllegalStateException("Unexpected nonmuxed tracker callback"));}
    public void reportAbuse(I2PSession s,int severity){failure.compareAndSet(null,new IllegalStateException("Unexpected router abuse event"));}
    public void disconnected(I2PSession s){if(!stopping)failure.compareAndSet(null,new IllegalStateException("Original tracker disconnected"));}
    public void errorOccurred(I2PSession s,String message,Throwable error){if(!stopping)failure.compareAndSet(null,new IllegalStateException(message,error));}
  }
  static void tracker(int port)throws Exception{
    require(!I2PAppContext.getGlobalContext().isRouterContext(),"Tracker must use actual external I2CP TCP, never internal queue");
    I2PClient client=I2PClientFactory.createClient();ByteArrayOutputStream nativeKeys=new ByteArrayOutputStream(),trackerKeys=new ByteArrayOutputStream();
    Destination nativeDest=client.createDestination(nativeKeys,SigType.EdDSA_SHA512_Ed25519),trackerDest=client.createDestination(trackerKeys,SigType.EdDSA_SHA512_Ed25519);
    bytes("native.keys",nativeKeys.toByteArray());bytes("native.dest",serialize(nativeDest));bytes("tracker.dest",serialize(trackerDest));
    Properties p=zeroHop();p.setProperty(I2PClient.PROP_TCP_HOST,"127.0.0.1");p.setProperty(I2PClient.PROP_TCP_PORT,Integer.toString(port));
    I2PSession session=client.createSession(new ByteArrayInputStream(trackerKeys.toByteArray()),p);Tracker tracker=new Tracker(session,nativeDest);session.addMuxedSessionListener(tracker,19,6969);session.addMuxedSessionListener(tracker,20,6969);
    try{
      session.connect();require(!session.isClosed()&&session.getMyDestination().equals(trackerDest),"Original tracker session not connected");
      output("tracker-ready.json","{\"status\":\"ACTUAL_EXTERNAL_TRACKER_CONNECTED\",\"tracker_destination_sha256\":\""+hex(trackerDest.calculateHash().getData())+"\"}");
      require("stop".equals(new BufferedReader(new InputStreamReader(System.in,java.nio.charset.StandardCharsets.US_ASCII)).readLine()),"Unexpected tracker control command");
      if(tracker.failure.get()!=null)throw new IllegalStateException("Original Java tracker callback failed",tracker.failure.get());
      synchronized(tracker){require(tracker.connects==1&&tracker.scrapes==1&&tracker.replies==2,"Actual original tracker wire coverage incomplete");}
      tracker.stopping=true;session.destroySession();require(session.isClosed(),"Original tracker session did not close");
      output("tracker-result.json","{\"status\":\"PASS\",\"signed_connects\":1,\"claimed_scrapes\":1,\"raw_replies\":2,\"external_i2cp\":true}");
    }finally{tracker.stopping=true;if(!session.isClosed())session.destroySession();}
  }
  public static void main(String[] args){int result=1;try{
    require(args.length==3,"router|tracker ABSOLUTE_OWNED_ROOT RANDOM_PORT");root=Paths.get(args[1]);require(root.isAbsolute()&&root.equals(Paths.get("/work")),"Only fenced /work accepted");int port=Integer.parseInt(args[2]);require(port>=1024&&port<=65535,"Random nonprivileged port required");origins();
    if(args[0].equals("router"))router(port);else{require(args[0].equals("tracker"),"Unknown subject");tracker(port);}result=0;
  }catch(Throwable e){e.printStackTrace(System.err);}finally{System.exit(result);}}
}
