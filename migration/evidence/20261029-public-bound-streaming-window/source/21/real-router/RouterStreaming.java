// Test-only actual external I2CP Streaming peer; router/fence come unchanged
// from the separately retained and actually qualified RouterTracker fixture.
import java.io.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
import java.util.concurrent.atomic.*;
import net.i2p.I2PAppContext;
import net.i2p.client.*;
import net.i2p.client.streaming.*;
import net.i2p.client.streaming.impl.I2PSocketManagerFull;
import net.i2p.crypto.SigType;
import net.i2p.data.*;

public final class RouterStreaming {
  static final byte[] REQUEST="GET /owned-stream HTTP/1.0\r\nHost: owned.i2p\r\n\r\n".getBytes(StandardCharsets.US_ASCII);
  static byte[] response(){
    byte[] head="HTTP/1.0 200 OK\r\nContent-Length: 8192\r\nConnection: close\r\n\r\n".getBytes(StandardCharsets.US_ASCII);
    byte[] out=Arrays.copyOf(head,head.length+8192);for(int i=0;i<8192;i++)out[head.length+i]=(byte)(i*31+7);return out;
  }
  static void peer(int port)throws Exception{
    RouterTracker.require(!I2PAppContext.getGlobalContext().isRouterContext(),"External Streaming peer must use real I2CP TCP");
    for(Class<?> c:new Class<?>[]{I2PSocketManagerFull.class,I2PSocket.class,I2PSession.class})RouterTracker.origin(c);
    I2PClient client=I2PClientFactory.createClient();ByteArrayOutputStream nativeKeys=new ByteArrayOutputStream(),peerKeys=new ByteArrayOutputStream();
    Destination nativeDest=client.createDestination(nativeKeys,SigType.EdDSA_SHA512_Ed25519),peerDest=client.createDestination(peerKeys,SigType.EdDSA_SHA512_Ed25519);
    RouterTracker.bytes("native.keys",nativeKeys.toByteArray());RouterTracker.bytes("native.dest",RouterTracker.serialize(nativeDest));RouterTracker.bytes("tracker.dest",RouterTracker.serialize(peerDest));
    Properties p=RouterTracker.zeroHop();p.setProperty(I2PClient.PROP_TCP_HOST,"127.0.0.1");p.setProperty(I2PClient.PROP_TCP_PORT,Integer.toString(port));
    p.setProperty("i2p.streaming.enforceProtocol","true");p.setProperty("i2p.streaming.answerPings","false");p.setProperty("i2p.streaming.maxMessageSize","1730");
    p.setProperty("i2p.streaming.initialWindowSize","1");p.setProperty("i2p.streaming.maxWindowSize","4");p.setProperty("i2p.streaming.readTimeout","30000");p.setProperty("i2p.streaming.writeTimeout","30000");p.setProperty("i2p.streaming.inactivityTimeout","60000");
    I2PSession session=client.createSession(new ByteArrayInputStream(peerKeys.toByteArray()),p);
    I2PSocketManagerFull manager=new I2PSocketManagerFull(I2PAppContext.getGlobalContext(),session,p,"OwnedRealRouterStreaming",d->d.equals(nativeDest));
    AtomicReference<Throwable> failure=new AtomicReference<>();AtomicBoolean completed=new AtomicBoolean();
    try{
      session.connect();RouterTracker.require(!session.isClosed()&&session.getMyDestination().equals(peerDest),"Actual external session not connected");
      I2PServerSocket server=manager.getServerSocket();server.setSoTimeout(90000);
      Thread application=new Thread(()->{try{
        try(I2PSocket socket=server.accept()){
          RouterTracker.require(socket.getPeerDestination().equals(nativeDest)&&socket.getLocalPort()==80&&socket.getPort()==9000,"Wrong real Streaming peer/ports");socket.setReadTimeout(30000);
          InputStream in=socket.getInputStream();RouterTracker.require(Arrays.equals(in.readNBytes(REQUEST.length),REQUEST),"Wrong real HTTP-shaped request");RouterTracker.require(in.read()==-1,"Missing real native request FIN");
          OutputStream out=socket.getOutputStream();out.write(response());out.flush();out.close();
        }
        completed.set(true);RouterTracker.output("stream-application.json","{\"status\":\"PASS\",\"request_bytes\":"+REQUEST.length+",\"response_body_bytes\":8192,\"external_i2cp\":true}");
      }catch(Throwable t){failure.compareAndSet(null,t);}},"owned-real-streaming-application");application.setDaemon(true);application.start();
      RouterTracker.output("tracker-ready.json","{\"status\":\"ACTUAL_EXTERNAL_STREAMING_CONNECTED\",\"peer_destination_sha256\":\""+RouterTracker.hex(peerDest.calculateHash().getData())+"\"}");
      RouterTracker.require("stop".equals(new BufferedReader(new InputStreamReader(System.in,StandardCharsets.US_ASCII)).readLine()),"Unexpected Streaming peer owner command");
      application.join(5000);if(failure.get()!=null)throw new IllegalStateException("Actual Streaming application failed",failure.get());RouterTracker.require(completed.get(),"Actual Streaming application unfinished");
      manager.destroySocketManager();RouterTracker.require(session.isClosed(),"External Streaming session not retired");
      RouterTracker.output("tracker-result.json","{\"status\":\"PASS\",\"scope\":\"actual-external-manager-http-shaped-stream\",\"body_bytes\":8192,\"external_i2cp\":true,\"session_closed\":true}");
    }finally{manager.destroySocketManager();if(!session.isClosed())session.destroySession();}
  }
  public static void main(String[] args){int exit=1;try{
    RouterTracker.require(args.length==3,"router|tracker /work RANDOM_PORT");RouterTracker.root=Paths.get(args[1]);RouterTracker.require(RouterTracker.root.equals(Paths.get("/work")),"Only owned fenced /work");
    int port=Integer.parseInt(args[2]);RouterTracker.require(port>=1024&&port<=65535,"Random nonprivileged port required");RouterTracker.origins();
    if(args[0].equals("router"))RouterTracker.router(port);else{RouterTracker.require(args[0].equals("tracker"),"Unknown subject");peer(port);}exit=0;
  }catch(Throwable t){t.printStackTrace(System.err);}finally{System.exit(exit);}}
}
