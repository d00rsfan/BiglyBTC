import java.io.*;
public class TrackerHeaderCheck {
 public static void main(String[] args)throws Exception {
  for(byte[] bytes:new byte[][]{new byte[0],"GET /announce?x".getBytes(java.nio.charset.StandardCharsets.US_ASCII)}){
   try{RouterMultiTorrentTracker.readHeader(new ByteArrayInputStream(bytes));throw new AssertionError("EOF accepted as request");}catch(EOFException expected){}
  }
  try{RouterMultiTorrentTracker.readHeader(new ByteArrayInputStream(new byte[]{1}));throw new AssertionError("Invalid byte accepted");}catch(IllegalStateException expected){}
  try{RouterMultiTorrentTracker.readHeader(new InputStream(){public int read()throws IOException{throw new java.net.SocketTimeoutException();}});throw new AssertionError("Timeout accepted as header");}catch(java.net.SocketTimeoutException expected){}
  String hash="%01".repeat(20);String valid="GET /announce?info_hash="+hash+"&peer_id="+hash+"&compact=1 HTTP/1.1\r\nHost: example.i2p\r\n\r\n";
  var request=RouterMultiTorrentTracker.query(RouterMultiTorrentTracker.readHeader(new ByteArrayInputStream(valid.getBytes(java.nio.charset.StandardCharsets.US_ASCII))));
  if(request.scrape||request.fields.get("info_hash").length!=20)throw new AssertionError("Valid request lost");
  try{RouterMultiTorrentTracker.query("GET /announce?info_hash=x HTTP/1.0\r\nHost: x\r\n\r\n".getBytes());throw new AssertionError("Wrong identity accepted");}catch(IllegalStateException expected){}
  System.out.println("PASS EOF0, partial EOF, invalid ASCII, timeout propagation, valid full hash, short hash refusal");
 }
}
