// Test-only real modified-core Streaming engine behind a finite I2PSession
// packet-routing adapter. This does not create a router, socket or I2CP session.
import java.io.*;
import java.lang.reflect.*;
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.*;
import net.i2p.I2PAppContext;
import net.i2p.client.*;
import net.i2p.client.streaming.*;
import net.i2p.client.streaming.impl.I2PSocketManagerFull;
import net.i2p.crypto.SigType;
import net.i2p.data.*;

public final class StreamingPeer {
  static final int MAX_PACKET=32768,MAX_MUX=40960,MAX_FRAMES=4096;
  static final int UPLOAD_SIZE=128*1024;
  static final byte[] UPLOAD_HEADER=("POST /owned-upload HTTP/1.0\r\nHost: owned.i2p\r\nContent-Length: "+UPLOAD_SIZE+"\r\n\r\n").getBytes(StandardCharsets.US_ASCII);
  static final byte[] REQUEST="GET /owned-stream HTTP/1.0\r\nHost: owned.i2p\r\n\r\n".getBytes(StandardCharsets.US_ASCII);
  // Diagnostic-only: keep the same first-failure CAS and existing main exit.
  static void reportAsync(AtomicReference<Throwable> target,Throwable failure){
    if(target.compareAndSet(null,failure))try{synchronized(System.err){
      System.err.println("OWNED_ASYNC_FAILURE thread="+Thread.currentThread().getName());failure.printStackTrace(System.err);System.err.flush();
    }}catch(Throwable ignored){} // Reporting must not replace the caught cause.
  }
  static void ownedLogging(Path root,Properties contextOptions)throws IOException{
    Properties log=new Properties();
    log.setProperty("logger.logFileName",root.resolve("owned-streaming-@.log").toString());
    log.setProperty("logger.logFileSize","65536");
    // This actual JAR rotates inclusively through0..limit, hence two slots.
    log.setProperty("logger.logRotationLimit","1");log.setProperty("logger.gzip","false");
    log.setProperty("logger.defaultLevel","WARN");log.setProperty("logger.minimumOnScreenLevel","CRIT");
    log.setProperty("logger.record.net.i2p.client.streaming.impl.ConnectionManager","DEBUG");
    log.setProperty("logger.record.net.i2p.client.streaming.impl.ConnectionPacketHandler","DEBUG");
    log.setProperty("logger.flushInterval","1");
    Path config=root.resolve("owned-logger.config");
    try(OutputStream out=Files.newOutputStream(config,StandardOpenOption.CREATE_NEW,StandardOpenOption.WRITE)){log.store(out,"Owned fixture diagnostic logging only");}
    contextOptions.setProperty("loggerConfigLocation",config.toString());
  }
  // This is only a fixture callback-order barrier, never a peer ACK.
  static boolean closeFlag(byte[] packet){
    if(packet.length<22)return false;int nacks=packet[16]&255,header=22+4*nacks;
    if(header>packet.length)return false;
    int flags=((packet[18+4*nacks]&255)<<8)|(packet[19+4*nacks]&255);
    int options=((packet[20+4*nacks]&255)<<8)|(packet[21+4*nacks]&255);
    return options<=packet.length-header&&(flags&10)==10;
  }
  static void require(boolean ok,String why){if(!ok)throw new IllegalStateException(why);}
  static byte[] read(Path p)throws Exception{byte[] b=Files.readAllBytes(p);require(b.length>0&&b.length<=1024,"Bounded synthetic fixture required");return b;}
  static Destination destination(Path p)throws Exception{ByteArrayInputStream in=new ByteArrayInputStream(read(p));Destination d=Destination.create(in);require(in.available()==0,"Trailing Destination");return d;}
  static void output(Path root,String leaf,String json)throws Exception{
    Path p=root.resolve(leaf+".tmp");Files.write(p,(json+"\n").getBytes(StandardCharsets.UTF_8),StandardOpenOption.CREATE_NEW);Files.move(p,root.resolve(leaf),StandardCopyOption.ATOMIC_MOVE);
  }
  static byte[] response(boolean backpressure){int size=backpressure?96*1024:8192;byte[] header=("HTTP/1.0 200 OK\r\nContent-Length: "+size+"\r\nConnection: close\r\n\r\n").getBytes(StandardCharsets.US_ASCII);byte[] out=Arrays.copyOf(header,header.length+size);for(int i=0;i<size;i++)out[header.length+i]=(byte)(i*31+7);return out;}
  static String hex(byte[] bytes){StringBuilder out=new StringBuilder(bytes.length*2);for(byte value:bytes){out.append(Character.forDigit((value>>>4)&15,16));out.append(Character.forDigit(value&15,16));}return out.toString();}
  static String receiveUpload(I2PSocket socket,Adapter route)throws Exception{
    InputStream input=socket.getInputStream();require(Arrays.equals(input.readNBytes(UPLOAD_HEADER.length),UPLOAD_HEADER),"Wrong actual upload header");
    MessageDigest digest=MessageDigest.getInstance("SHA-256");byte[] buffer=new byte[4096];int received=0;
    while(received<UPLOAD_SIZE){int n=input.read(buffer,0,Math.min(buffer.length,UPLOAD_SIZE-received));require(n>0,"Truncated actual128KiB upload");for(int i=0;i<n;i++)require(buffer[i]==(byte)((received+i)*17+11),"Actual upload byte mismatch");digest.update(buffer,0,n);received+=n;}
    require(input.read()==-1,"Native FIN must follow exact128KiB upload");route.awaitNativeFin();String value=hex(digest.digest());byte[] body=(value+"\n").getBytes(StandardCharsets.US_ASCII);
    OutputStream output=socket.getOutputStream();output.write(("HTTP/1.0 200 OK\r\nContent-Length: "+body.length+"\r\nConnection: close\r\n\r\n").getBytes(StandardCharsets.US_ASCII));output.write(body);output.flush();output.close();return value;
  }
  static final class Adapter implements InvocationHandler {
    final CountDownLatch nativeFinProcessed=new CountDownLatch(1);
    void awaitNativeFin()throws InterruptedException{require(nativeFinProcessed.await(30,TimeUnit.SECONDS),"Native FIN callback did not finish for port9000");}
    final Destination local,remote;final SigningPrivateKey key;
    final I2PSession session;final ArrayBlockingQueue<byte[]> outgoing=new ArrayBlockingQueue<>(16);
    final AtomicReference<Throwable> failure=new AtomicReference<>();
    final AtomicInteger sent=new AtomicInteger(),received=new AtomicInteger();
    final AtomicBoolean closed=new AtomicBoolean();
    volatile I2PSessionMuxedListener listener;int listenerProtocol,listenerPort,nextMessage=1;
    int pendingId;byte[] pending;long rawSent,rawReceived;
    Adapter(Destination local,Destination remote,SigningPrivateKey key){this.local=local;this.remote=remote;this.key=key;session=(I2PSession)Proxy.newProxyInstance(I2PSession.class.getClassLoader(),new Class<?>[]{I2PSession.class},this);}
    public Object invoke(Object proxy,Method method,Object[] a)throws Throwable{
      if(method.getDeclaringClass()==Object.class){switch(method.getName()){
        case "toString":return "Explicit protocol6 fixture adapter (no router)";
        case "hashCode":return System.identityHashCode(proxy);
        case "equals":return proxy==a[0];
      }}
      switch(method.getName()){
        case "getMyDestination":return local;
        case "getPrivateKey":return key;
        case "isOffline":return false;
        case "isClosed":return closed.get();
        case "getSubsessions":return Collections.emptyList();
        case "addMuxedSessionListener":{
          // Original manager registers protocol6/default-port0 (all). This
          // adapter still permits ONLY the known actual destination port80.
          require(a.length==3&&((Integer)a[1])==6&&((Integer)a[2])==0,"Only original default protocol6 listener");
          require(listener==null||listener==a[0],"Unexpected listener replacement");listenerProtocol=(Integer)a[1];listenerPort=(Integer)a[2];listener=(I2PSessionMuxedListener)a[0];return null;
        }
        case "removeListener":require(a.length==2&&((Integer)a[0])==6&&((Integer)a[1])==0,"Wrong listener removal");listener=null;return null;
        case "receiveMessage":{
          synchronized(this){require(a.length==1&&pending!=null&&((Integer)a[0])==pendingId,"Wrong/duplicate receiveMessage");byte[] b=pending;pending=null;pendingId=0;return b;}
        }
        case "sendMessage":try{return send(a);}catch(Throwable t){reportAsync(failure,t);throw new I2PSessionException("Fixture protocol6 route failed",t);}
        case "destroySession":closed.set(true);return null;
        default:throw new UnsupportedOperationException("Unexpected fixture I2PSession call: "+method.toGenericString());
      }
    }
    Object send(Object[] a)throws Exception{
      // This is the real PacketQueue's inbound-server, no-status overload.
      // No fabricated ACCEPTED/DELIVERED/status-listener callback is provided.
      require(!closed.get()&&a!=null&&a.length==8&&remote.equals(a[0]),"Wrong packet target/session or unsupported status send");
      byte[] packet=(byte[])a[1];int off=(Integer)a[2],size=(Integer)a[3],proto=(Integer)a[4],from=(Integer)a[5],to=(Integer)a[6];SendMessageOptions options=(SendMessageOptions)a[7];
      require(off>=0&&size>=22&&size<=MAX_PACKET&&off<=packet.length-size&&proto==6&&from==80&&to==9000,"Wrong actual Streaming packet/ports");
      boolean gzip=options.getGzip()==SendMessageOptions.GzipOption.GZIP_ON||(options.getGzip()==SendMessageOptions.GzipOption.DEFAULT&&size>66);
      // Exact original session prepPayload policy: real DataHelper gzip, then
      // ports in MTIME bytes4..7 and protocol in OS byte9. Not application data.
      byte[] mux=gzip?DataHelper.compress(packet,off,size):DataHelper.compress(packet,off,size,DataHelper.NO_COMPRESSION);
      require(mux.length>=20&&mux.length<=MAX_MUX,"Unbounded original mux gzip");mux[4]=(byte)(from>>8);mux[5]=(byte)from;mux[6]=(byte)(to>>8);mux[7]=(byte)to;mux[9]=6;
      require(sent.incrementAndGet()<=MAX_FRAMES,"Outgoing packet lifetime bound");
      require(outgoing.offer(mux),"Outgoing packet-routing backpressure");synchronized(this){rawSent+=size;}return true; // Queueing only.
    }
    void receive(byte[] mux)throws Exception{
      require(!closed.get()&&mux.length>=20&&mux.length<=MAX_MUX&&(mux[9]&255)==6,"Wrong routed mux frame");
      int from=((mux[4]&255)<<8)|(mux[5]&255),to=((mux[6]&255)<<8)|(mux[7]&255);require(from==9000&&to==80,"Wrong routed Streaming ports");
      byte[] packet=DataHelper.decompress(mux);require(packet!=null&&packet.length>=22&&packet.length<=MAX_PACKET,"Unbounded or empty routed packet");boolean nativeClose=closeFlag(packet);
      I2PSessionMuxedListener target=listener;require(target!=null&&listenerProtocol==6&&listenerPort==0,"No actual manager protocol6 listener");
      int id;synchronized(this){require(pending==null&&nextMessage<=MAX_FRAMES,"Inbound route capacity/lifetime bound");pending=packet;id=pendingId=nextMessage++;rawReceived+=packet.length;}
      received.incrementAndGet();target.messageAvailable(session,id,packet.length,6,from,to);
      synchronized(this){require(pending==null,"Actual manager did not consume routed packet");}if(nativeClose)nativeFinProcessed.countDown();
    }
  }
  public static void main(String[] args){
    final OutputStream wireOut=new FileOutputStream(FileDescriptor.out);System.setOut(System.err);
    int exit=1;I2PSocketManagerFull manager=null;Adapter adapter=null;AtomicReference<Throwable> appFailure=new AtomicReference<>();AtomicBoolean appDone=new AtomicBoolean();AtomicReference<String> uploadDigest=new AtomicReference<>();Path root=null;
    try{
      require(args.length==4,"http|recovery|backpressure|reset|cancel|upload OWNED_ROOT FIXTURES MODIFIED_JAR");String mode=args[0];require(mode.equals("http")||mode.equals("recovery")||mode.equals("backpressure")||mode.equals("reset")||mode.equals("cancel")||mode.equals("upload"),"Unsupported peer mode");
      root=Paths.get(args[1]);require(root.isAbsolute()&&Files.isDirectory(root),"Owned absolute output required");Path fixtures=Paths.get(args[2]);
      for(Class<?> c:new Class<?>[]{I2PSocketManagerFull.class,I2PSession.class,Destination.class,DataHelper.class})require(c.getProtectionDomain().getCodeSource().getLocation().toURI().equals(Paths.get(args[3]).toUri()),"Wrong modified class origin: "+c.getName());
      Properties contextOptions=new Properties();contextOptions.setProperty("time.disabled","true");contextOptions.setProperty("i2p.dir.config",root.toString());contextOptions.setProperty("i2p.dir.base",root.toString());
      ownedLogging(root,contextOptions);I2PAppContext context=new I2PAppContext(contextOptions);Destination local=destination(fixtures.resolve("ed25519.sender")),remote=destination(fixtures.resolve("dsa.sender"));
      adapter=new Adapter(local,remote,new SigningPrivateKey(SigType.EdDSA_SHA512_Ed25519,read(fixtures.resolve("ed25519.known-test-key"))));
      Properties options=new Properties();options.setProperty("i2p.streaming.enforceProtocol","true");options.setProperty("i2p.streaming.answerPings","false");options.setProperty("i2p.streaming.maxMessageSize","1730");options.setProperty("i2p.streaming.initialWindowSize","1");options.setProperty("i2p.streaming.maxWindowSize","4");options.setProperty("i2p.streaming.readTimeout","30000");options.setProperty("i2p.streaming.writeTimeout","30000");options.setProperty("i2p.streaming.inactivityTimeout","60000");options.setProperty("i2p.streaming.bufferSize","65536");
      // A supported explicit test option makes genuine response packets
      // concurrently available for controlled wire reordering. Production
      // defaults are not changed by this fixture.
      if(mode.equals("recovery"))options.setProperty("i2p.streaming.initialWindowSize","4");
      manager=new I2PSocketManagerFull(context,adapter.session,options,"ExplicitNativeInterop",peer->peer.equals(remote));I2PServerSocket server=manager.getServerSocket();server.setSoTimeout(60000);
      final Adapter route=adapter;final Path outputRoot=root;
      Thread writer=new Thread(()->{try{DataOutputStream out=new DataOutputStream(wireOut);while(true){byte[] mux=route.outgoing.take();out.writeInt(mux.length);out.write(mux);out.flush();}}catch(Throwable t){reportAsync(route.failure,t);}},"original-peer-packets");writer.setDaemon(true);writer.start();
      Thread application=new Thread(()->{try{
        try(I2PSocket socket=server.accept()){
          require(socket.getPeerDestination().equals(remote)&&socket.getLocalPort()==80&&socket.getPort()==9000,"Wrong actual accepted Streaming connection");socket.setReadTimeout(30000);
          if(mode.equals("upload")){uploadDigest.set(receiveUpload(socket,route));}
          else if(mode.equals("reset")){socket.reset();}
          else if(mode.equals("cancel")){
            // Native cancellation is LOCAL ONLY. Queue genuine late peer data;
            // the C++ fixture holds it until after cancellation. No timeout or
            // generic IOException is credited as a remote cancellation proof.
            require(socket.getInputStream().read()=='C',"Missing actual cancel-control application byte");
            socket.getOutputStream().write("late-peer-bytes".getBytes(StandardCharsets.US_ASCII));socket.getOutputStream().flush();
          }else{
            InputStream input=socket.getInputStream();byte[] request=input.readNBytes(REQUEST.length);require(Arrays.equals(request,REQUEST),"Wrong actual HTTP-shaped request bytes");require(input.read()==-1,"Native FIN must follow exact request");route.awaitNativeFin();
            OutputStream output=socket.getOutputStream();output.write(response(mode.equals("backpressure")));output.flush();output.close();
          }
        }
        appDone.set(true);output(outputRoot,"peer-application.json","{\"status\":\"PASS\",\"mode\":\""+mode+"\",\"actual_manager_socket\":true"+(mode.equals("upload")?",\"upload_bytes\":"+UPLOAD_SIZE+",\"upload_sha256\":\""+uploadDigest.get()+"\"":"")+"}");
      }catch(Throwable t){reportAsync(appFailure,t);}},"original-peer-application");application.setDaemon(true);application.start();
      output(root,"peer-ready.json","{\"status\":\"ACTUAL_MANAGER_PROTOCOL6_LISTENER\",\"router\":false,\"session_adapter_only\":true}");
      // Only real protocol6 payloads traverse stdin/stdout. EOF is a fixture
      // transport shutdown, not an invented application FIN or protocol ACK.
      DataInputStream in=new DataInputStream(System.in);while(true){
        int first=in.read();if(first<0)break;int length=(first<<24)|(in.readUnsignedByte()<<16)|(in.readUnsignedByte()<<8)|in.readUnsignedByte();require(length>=20&&length<=MAX_MUX,"Unbounded packet frame");byte[] mux=new byte[length];in.readFully(mux);adapter.receive(mux);
        if(adapter.failure.get()!=null)throw new IllegalStateException("Packet writer failed",adapter.failure.get());if(appFailure.get()!=null)throw new IllegalStateException("Actual peer application failed",appFailure.get());
      }
      application.join(3000);if(appFailure.get()!=null)throw new IllegalStateException("Actual peer application failed",appFailure.get());if(adapter.failure.get()!=null)throw new IllegalStateException("Packet route failed",adapter.failure.get());require(appDone.get(),"Bridge EOF before original peer application completed");
      require(adapter.sent.get()>0&&adapter.received.get()>0,"No real original Streaming packet exchange");
      output(root,"peer-result.json","{\"status\":\"PASS\",\"mode\":\""+mode+"\",\"frames_sent\":"+adapter.sent.get()+",\"frames_received\":"+adapter.received.get()+",\"raw_bytes_sent\":"+adapter.rawSent+",\"raw_bytes_received\":"+adapter.rawReceived+",\"router_or_tunnels_claimed\":false}");exit=0;
    }catch(Throwable t){t.printStackTrace(System.err);}finally{
      if(manager!=null)try{manager.destroySocketManager();}catch(Throwable t){t.printStackTrace(System.err);exit=1;}
      if(adapter!=null)adapter.closed.set(true);System.exit(exit);
    }
  }
}
