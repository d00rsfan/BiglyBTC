// Test-only actual modified-core Streaming manager. No router or real session.
import java.io.*;
import java.lang.reflect.*;
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.util.*;
import java.util.concurrent.*;
import java.util.concurrent.atomic.*;
import net.i2p.I2PAppContext;
import net.i2p.client.*;
import net.i2p.client.streaming.*;
import net.i2p.client.streaming.impl.I2PSocketManagerFull;
import net.i2p.crypto.SigType;
import net.i2p.data.*;

public final class ManagerPeer {
  static final int ROUNDS=26,MAX_PACKET=32768,MAX_MUX=40960,MAX_FRAMES=4096;
  static final long MIN_ACTIVITY_NS=125000000000L;
  static void require(boolean ok,String reason){if(!ok)throw new IllegalStateException(reason);}
  static byte[] bytes(Path p)throws Exception{byte[] b=Files.readAllBytes(p);require(b.length>0&&b.length<=1024,"Bounded synthetic fixture required");return b;}
  static Destination destination(Path p)throws Exception{ByteArrayInputStream in=new ByteArrayInputStream(bytes(p));Destination d=Destination.create(in);require(in.available()==0,"Trailing Destination");return d;}
  static byte[] ascii(String s){return s.getBytes(StandardCharsets.US_ASCII);}
  static void output(Path root,String leaf,String json)throws Exception{Path p=root.resolve(leaf+".tmp");Files.write(p,(json+"\n").getBytes(StandardCharsets.UTF_8),StandardOpenOption.CREATE_NEW);Files.move(p,root.resolve(leaf),StandardCopyOption.ATOMIC_MOVE);}
  static byte[] request(int round){return ascii("GET /manager/"+round+" HTTP/1.1\r\nHost: owned.i2p\r\nConnection: keep-alive\r\n\r\n");}
  static byte[] response(int port,int round){byte[] header=ascii("HTTP/1.1 200 OK\r\nContent-Length: 512\r\nConnection: keep-alive\r\n\r\n");byte[] b=Arrays.copyOf(header,header.length+512);for(int i=0;i<512;i++)b[header.length+i]=(byte)(port+round*13+i);return b;}
  static byte[] handshake(){byte[] b=new byte[68];b[0]=19;System.arraycopy(ascii("BitTorrent protocol"),0,b,1,19);for(int i=28;i<68;i++)b[i]=(byte)(i*7+3);return b;}
  static boolean javaRoute(int from,int to){return (from==80&&(to==9000||to==9001||to==9002))||(from==8020&&to==6881);}
  static final class Adapter implements InvocationHandler {
    final Destination local,remote;final SigningPrivateKey key;final I2PSession session;
    final ArrayBlockingQueue<byte[]> outgoing=new ArrayBlockingQueue<>(16);
    final AtomicReference<Throwable> failure=new AtomicReference<>();
    final AtomicInteger sent=new AtomicInteger(),received=new AtomicInteger(),statusEnqueues=new AtomicInteger();
    final AtomicBoolean closed=new AtomicBoolean();volatile I2PSessionMuxedListener listener;
    int nextMessage=1,pendingId;long nextEnqueue=1;byte[] pending;long rawSent,rawReceived;
    Adapter(Destination local,Destination remote,SigningPrivateKey key){this.local=local;this.remote=remote;this.key=key;session=(I2PSession)Proxy.newProxyInstance(I2PSession.class.getClassLoader(),new Class<?>[]{I2PSession.class},this);}
    public Object invoke(Object proxy,Method method,Object[] a)throws Throwable{
      if(method.getDeclaringClass()==Object.class){switch(method.getName()){
        case "toString":return "Explicit four-stream packet adapter, no router";
        case "hashCode":return System.identityHashCode(proxy);
        case "equals":return proxy==a[0];
      }}
      switch(method.getName()){
        case "getMyDestination":return local;
        case "getPrivateKey":return key;
        case "isOffline":return false;
        case "isClosed":return closed.get();
        case "getSubsessions":return Collections.emptyList();
        case "addMuxedSessionListener":require(a.length==3&&((Integer)a[1])==6&&((Integer)a[2])==0&&(listener==null||listener==a[0]),"Wrong actual mux listener");listener=(I2PSessionMuxedListener)a[0];return null;
        case "removeListener":require(a.length==2&&((Integer)a[0])==6&&((Integer)a[1])==0,"Wrong listener removal");listener=null;return null;
        case "receiveMessage":synchronized(this){require(a.length==1&&pending!=null&&((Integer)a[0])==pendingId,"Wrong/duplicate packet receive");byte[] b=pending;pending=null;pendingId=0;return b;}
        case "sendMessage":try{return send(a);}catch(Throwable t){failure.compareAndSet(null,t);throw new I2PSessionException("Fixture protocol6 route failed",t);}
        case "destroySession":closed.set(true);return null;
        default:throw new UnsupportedOperationException("Unexpected session API: "+method.toGenericString());
      }
    }
    Object send(Object[] a)throws Exception{
      require(!closed.get()&&a!=null&&(a.length==8||a.length==9)&&remote.equals(a[0]),"Wrong full peer/session/send overload");
      byte[] packet=(byte[])a[1];int off=(Integer)a[2],size=(Integer)a[3],proto=(Integer)a[4],from=(Integer)a[5],to=(Integer)a[6];SendMessageOptions options=(SendMessageOptions)a[7];
      require(off>=0&&size>=22&&size<=MAX_PACKET&&off<=packet.length-size&&proto==6&&javaRoute(from,to),"Wrong actual protocol/packet/port scope");
      if(a.length==9)require(a[8] instanceof SendMessageStatusListener,"Wrong status-listener overload");
      boolean gzip=options.getGzip()==SendMessageOptions.GzipOption.GZIP_ON||(options.getGzip()==SendMessageOptions.GzipOption.DEFAULT&&size>66);
      byte[] mux=gzip?DataHelper.compress(packet,off,size):DataHelper.compress(packet,off,size,DataHelper.NO_COMPRESSION);
      require(mux.length>=20&&mux.length<=MAX_MUX,"Unbounded actual mux gzip");mux[4]=(byte)(from>>8);mux[5]=(byte)from;mux[6]=(byte)(to>>8);mux[7]=(byte)to;mux[9]=6;
      require(sent.incrementAndGet()<=MAX_FRAMES&&outgoing.offer(mux),"Packet queue/lifetime exhausted");
      synchronized(this){rawSent+=size;if(a.length==9){require(nextEnqueue<=MAX_FRAMES,"Enqueue ID exhausted");statusEnqueues.incrementAndGet();return Long.valueOf(nextEnqueue++);}}
      // A real local enqueue only. Never invent ACCEPTED/DELIVERED or invoke a
      // status listener; PacketQueue's ID map receives no router-status claim.
      return Boolean.TRUE;
    }
    void receive(byte[] mux)throws Exception{
      require(!closed.get()&&mux.length>=20&&mux.length<=MAX_MUX&&(mux[9]&255)==6,"Wrong actual routed mux");
      int from=((mux[4]&255)<<8)|(mux[5]&255),to=((mux[6]&255)<<8)|(mux[7]&255);require(javaRoute(to,from),"Wrong routed port tuple");
      byte[] packet=DataHelper.decompress(mux);require(packet!=null&&packet.length>=22&&packet.length<=MAX_PACKET,"Unbounded routed packet");
      I2PSessionMuxedListener target=listener;require(target!=null,"No actual manager listener");int id;
      synchronized(this){require(pending==null&&nextMessage<=MAX_FRAMES,"Receive capacity/lifetime exhausted");pending=packet;id=pendingId=nextMessage++;rawReceived+=packet.length;}
      received.incrementAndGet();target.messageAvailable(session,id,packet.length,6,from,to);synchronized(this){require(pending==null,"Actual manager did not consume packet");}
    }
  }
  static void http(I2PSocket socket,Path root)throws Exception{
    int port=socket.getPort();require(socket.getLocalPort()==80&&(port==9000||port==9001||port==9002),"Wrong actual accepted ports");socket.setReadTimeout(30000);
    InputStream in=socket.getInputStream();OutputStream out=socket.getOutputStream();
    if(port==9002){require(in.read()=='C',"Missing cancel application byte");out.write(ascii("late-manager-peer-bytes"));out.flush();output(root,"cancel-application.json","{\"status\":\"PASS\",\"actual_manager_socket\":true}");return;}
    long first=0,last=0;for(int round=0;round<ROUNDS;round++){byte[] expected=request(round);require(Arrays.equals(in.readNBytes(expected.length),expected),"Wrong concurrent HTTP request");long at=System.nanoTime();if(round==0)first=at;last=at;out.write(response(port,round));out.flush();}
    require(last-first>=MIN_ACTIVITY_NS,"HTTP activity did not cross120s");require(in.read()==-1,"Native FIN missing after final HTTP round");out.close();
    output(root,"http-"+port+".json","{\"status\":\"PASS\",\"rounds\":"+ROUNDS+",\"actual_activity_ns\":"+(last-first)+",\"actual_manager_socket\":true}");
  }
  public static void main(String[] args){
    OutputStream wireOut=new FileOutputStream(FileDescriptor.out);System.setOut(System.err);int exit=1;I2PSocketManagerFull manager=null;Adapter adapter=null;
    AtomicReference<Throwable> applicationFailure=new AtomicReference<>();AtomicInteger completed=new AtomicInteger();List<Thread> applications=new ArrayList<>();
    try{
      require(args.length==3,"OWNED_ROOT FIXTURES MODIFIED_JAR required");Path root=Paths.get(args[0]),fixtures=Paths.get(args[1]);require(root.isAbsolute()&&Files.isDirectory(root),"Fresh owned output required");
      for(Class<?> c:new Class<?>[]{I2PSocketManagerFull.class,I2PSession.class,SendMessageStatusListener.class,Destination.class,DataHelper.class})require(c.getProtectionDomain().getCodeSource().getLocation().toURI().equals(Paths.get(args[2]).toUri()),"Wrong actual class origin: "+c.getName());
      for(String name:new String[]{"net.i2p.client.streaming.impl.PacketQueue","net.i2p.client.streaming.impl.I2PSocketFull"}){Class<?> c=Class.forName(name,false,I2PSocketManagerFull.class.getClassLoader());require(c.getProtectionDomain().getCodeSource().getLocation().toURI().equals(Paths.get(args[2]).toUri()),"Wrong actual manager class origin: "+name);}
      Properties contextOptions=new Properties();contextOptions.setProperty("time.disabled","true");contextOptions.setProperty("i2p.dir.config",root.toString());contextOptions.setProperty("i2p.dir.base",root.toString());I2PAppContext context=new I2PAppContext(contextOptions);
      Destination local=destination(fixtures.resolve("ed25519.sender")),remote=destination(fixtures.resolve("dsa.sender"));adapter=new Adapter(local,remote,new SigningPrivateKey(SigType.EdDSA_SHA512_Ed25519,bytes(fixtures.resolve("ed25519.known-test-key"))));
      Properties options=new Properties();options.setProperty("i2p.streaming.enforceProtocol","true");options.setProperty("i2p.streaming.answerPings","false");options.setProperty("i2p.streaming.maxMessageSize","1730");options.setProperty("i2p.streaming.initialWindowSize","1");options.setProperty("i2p.streaming.maxWindowSize","8");options.setProperty("i2p.streaming.readTimeout","30000");options.setProperty("i2p.streaming.writeTimeout","30000");options.setProperty("i2p.streaming.inactivityTimeout","90000");options.setProperty("i2p.streaming.bufferSize","65536");
      manager=new I2PSocketManagerFull(context,adapter.session,options,"ExplicitNativeManagerInterop",peer->peer.equals(remote));I2PServerSocket server=manager.getServerSocket();server.setSoTimeout(60000);final Adapter route=adapter;final I2PSocketManagerFull actual=manager;
      Thread writer=new Thread(()->{try{DataOutputStream out=new DataOutputStream(wireOut);while(true){byte[] mux=route.outgoing.take();out.writeInt(mux.length);out.write(mux);out.flush();}}catch(Throwable t){route.failure.compareAndSet(null,t);}},"actual-manager-packets");writer.setDaemon(true);writer.start();
      Thread acceptor=new Thread(()->{try{Set<Integer> ports=new HashSet<>();for(int i=0;i<3;i++){I2PSocket socket=server.accept();require(socket.getPeerDestination().equals(remote)&&ports.add(socket.getPort()),"Wrong/duplicate accepted stream");Thread worker=new Thread(()->{try(I2PSocket owned=socket){http(owned,root);completed.incrementAndGet();}catch(Throwable t){applicationFailure.compareAndSet(null,t);}},"actual-http-"+socket.getPort());worker.setDaemon(true);synchronized(applications){applications.add(worker);}worker.start();}}catch(Throwable t){applicationFailure.compareAndSet(null,t);}},"actual-manager-accept");acceptor.setDaemon(true);applications.add(acceptor);acceptor.start();
      Thread incoming=new Thread(()->{try{
        long end=System.nanoTime()+60000000000L;while(!Files.exists(root.resolve("start-inbound.json"))){require(System.nanoTime()<end,"Native listener was not armed");Thread.sleep(10);}
        I2PSocketOptions connect=actual.buildOptions();connect.setLocalPort(8020);connect.setPort(6881);connect.setConnectTimeout(30000);connect.setReadTimeout(30000);
        try(I2PSocket socket=actual.connect(remote,connect)){require(socket.getPeerDestination().equals(remote)&&socket.getLocalPort()==8020&&socket.getPort()==6881,"Wrong actual initiated stream");InputStream in=socket.getInputStream();OutputStream out=socket.getOutputStream();byte[] hello=handshake();out.write(hello);out.flush();require(Arrays.equals(in.readNBytes(68),hello),"Wrong native BT-shaped handshake response");long first=0,last=0;
          for(int round=0;round<ROUNDS;round++){byte[] keep=in.readNBytes(4);require(Arrays.equals(keep,new byte[4]),"Wrong bounded BT keepalive");long at=System.nanoTime();if(round==0)first=at;last=at;out.write(keep);out.flush();}
          require(last-first>=MIN_ACTIVITY_NS,"Inbound actual activity did not cross120s");require(in.read()==-1,"Native FIN missing on inbound stream");out.close();output(root,"inbound-application.json","{\"status\":\"PASS\",\"handshake_bytes\":68,\"rounds\":"+ROUNDS+",\"actual_activity_ns\":"+(last-first)+",\"actual_manager_socket\":true}");completed.incrementAndGet();
        }
      }catch(Throwable t){applicationFailure.compareAndSet(null,t);}},"actual-manager-initiate");incoming.setDaemon(true);synchronized(applications){applications.add(incoming);}incoming.start();
      output(root,"peer-ready.json","{\"status\":\"ACTUAL_MANAGER_PROTOCOL6_LISTENER\",\"router\":false,\"session_adapter_only\":true}");
      DataInputStream in=new DataInputStream(System.in);while(true){int first=in.read();if(first<0)break;int length=(first<<24)|(in.readUnsignedByte()<<16)|(in.readUnsignedByte()<<8)|in.readUnsignedByte();require(length>=20&&length<=MAX_MUX,"Unbounded framed protocol packet");byte[] mux=new byte[length];in.readFully(mux);adapter.receive(mux);if(adapter.failure.get()!=null)throw new IllegalStateException("Packet route failed",adapter.failure.get());if(applicationFailure.get()!=null)throw new IllegalStateException("Actual manager application failed",applicationFailure.get());}
      List<Thread> joined;synchronized(applications){joined=new ArrayList<>(applications);}for(Thread thread:joined){thread.join(3000);require(!thread.isAlive(),"Application thread failed to join");}
      if(applicationFailure.get()!=null)throw new IllegalStateException("Actual manager application failed",applicationFailure.get());if(adapter.failure.get()!=null)throw new IllegalStateException("Packet route failed",adapter.failure.get());require(completed.get()==4&&adapter.statusEnqueues.get()>0,"Missing real applications/outgoing status overload");
      output(root,"peer-result.json","{\"status\":\"PASS\",\"applications\":4,\"status_enqueue_ids\":"+adapter.statusEnqueues.get()+",\"fabricated_status_callbacks\":0,\"frames_sent\":"+adapter.sent.get()+",\"frames_received\":"+adapter.received.get()+",\"router_or_tunnels_claimed\":false}");exit=0;
    }catch(Throwable t){t.printStackTrace(System.err);}finally{if(manager!=null)try{manager.destroySocketManager();}catch(Throwable t){t.printStackTrace(System.err);exit=1;}if(adapter!=null)adapter.closed.set(true);System.exit(exit);}
  }
}
