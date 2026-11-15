// SPDX-License-Identifier: GPL-2.0-or-later
package com.biglybt.core.dht.db.impl;
import com.biglybt.core.dht.*;
import com.biglybt.core.dht.control.*;
import com.biglybt.core.dht.control.impl.DHTControlImpl;
import com.biglybt.core.dht.router.*;
import com.biglybt.core.dht.transport.*;
import com.biglybt.core.util.*;
import java.lang.reflect.*;
import java.net.*;
import java.nio.file.*;
import java.util.*;

/** Real processSurvey/doQuery/queriesComplete, controlled contact callbacks. */
public final class OriginalSurveyGraph {
 static Object defaults(Method m){return OriginalCacheRepublish.defaults(m);}
 static byte[] id(int n){byte[] b=new byte[20];b[0]=(byte)n;return b;}
 static String hex(byte[] b){StringBuilder s=new StringBuilder();for(byte x:b)s.append(String.format("%02x",x&255));return s.toString();}
 static final class Pending {DHTTransportContact contact;DHTTransportReplyHandler handler;List<byte[]> headers;Pending(DHTTransportContact c,DHTTransportReplyHandler h,List<byte[]> k){contact=c;handler=h;headers=k;}}
 @SuppressWarnings("unchecked")
 static Map<String,Object> run(String name,int k,int keys,int replication,String response,boolean includeLocal,boolean distant,boolean subnet,boolean tokens)throws Exception{
  OriginalCacheRepublish db=new OriginalCacheRepublish();List<Pending> pending=new ArrayList<>();List<Object> queries=new ArrayList<>(),stores=new ArrayList<>(),contacts=new ArrayList<>(),keyInputs=new ArrayList<>();int[] tokenCount={0};try{
   Field cf=DHTDBImpl.class.getDeclaredField("control");cf.setAccessible(true);DHTControl old=(DHTControl)cf.get(db);Method comparator=DHTControlImpl.class.getDeclaredMethod("computeAndCompareDistances2",byte[].class,byte[].class,byte[].class);comparator.setAccessible(true);
   DHTRouter router=OriginalCacheRepublish.proxy(DHTRouter.class,(p,m,a)->m.getName().equals("getK")?k:m.getName().equals("getID")?id(1):m.getName().equals("isID")?Arrays.equals(id(1),(byte[])a[0]):defaults(m));
   DHTControl control=OriginalCacheRepublish.proxy(DHTControl.class,(p,m,a)->{
    switch(m.getName()){
    case "getRouter":return router;
    case "computeAndCompareDistances":return comparator.invoke(null,a);
    case "putDirectEncodedKeys":{
     Map<String,Object> op=new TreeMap<>();op.put("contact",((DHTTransportContact)a[3]).getID());List<Object> opKeys=new ArrayList<>();for(byte[] key:(byte[][])a[0])opKeys.add(key);op.put("keys",opKeys);List<Object> versions=new ArrayList<>();for(DHTTransportValue[] set:(DHTTransportValue[][])a[2]){List<Object> v=new ArrayList<>();for(DHTTransportValue value:set){if(!Arrays.equals(((DHTDBValueImpl)value).getSender().getID(),db.local.getID()))throw new AssertionError("Original relay sender");v.add((long)value.getVersion());}versions.add(v);}op.put("versions",versions);stores.add(op);((DHTOperationListener)a[4]).complete(false);return null;
    }
    default:return m.invoke(old,a);
    }
   });db.setControl(control);
   ByteArrayHashMap<DHTTransportContact> survey=new ByteArrayHashMap<>();if(includeLocal)survey.put(id(1),db.local);
   for(int index=0;index<5;index++){final int n=index+2;final int number=distant?240+index:n;final byte protocol=(byte)(index==1&&name.startsWith("legacy")?25:index==1&&name.startsWith("negative")?-1:55);final InetSocketAddress address=new InetSocketAddress(name.startsWith("IPv6")?"2001:db8::"+n:"127."+(subnet&&index<2?88:100+index)+".1."+n,8000+n);
    DHTTransportContact c=OriginalCacheRepublish.proxy(DHTTransportContact.class,(p,m,a)->{
     switch(m.getName()){
     case "getID":return id(number);
     case "getInstanceID":return 1;
     case "getAddress":case "getTransportAddress":case "getExternalAddress":return address;
     case "getProtocolVersion":return protocol;
     case "getRandomID":return tokens?0:123;
     case "getRandomIDType":return 1;
     case "hashCode":return number;
     case "equals":return p==a[0];
     case "sendQueryStore":{
      List<byte[]> headers=new ArrayList<>();List<Object> groups=new ArrayList<>();for(Object[] group:(List<Object[]>)a[2]){byte[] prefix=(byte[])group[0];List<Object> encoded=new ArrayList<>();for(byte[] suffix:(List<byte[]>)group[1]){byte[] header=new byte[6];System.arraycopy(prefix,0,header,0,prefix.length);System.arraycopy(suffix,0,header,prefix.length,suffix.length);headers.add(header);encoded.add(suffix);}Map<String,Object> g=new TreeMap<>();g.put("prefix",prefix);g.put("suffixes",encoded);groups.add(g);}Map<String,Object> q=new TreeMap<>();q.put("contact",id(number));q.put("header",((Number)a[1]).longValue());q.put("groups",groups);queries.add(q);pending.add(new Pending((DHTTransportContact)p,(DHTTransportReplyHandler)a[0],headers));return null;
     }
     case "sendFindNode":tokenCount[0]++;((DHTTransportReplyHandler)a[0]).findNodeReply((DHTTransportContact)p,new DHTTransportContact[0]);return null;
     default:return defaults(m);
     }
    });survey.put(id(number),c);Map<String,Object> input=new TreeMap<>();input.put("id",id(number));input.put("ip",address.getAddress().getAddress());input.put("protocol",(long)protocol);contacts.add(input);
   }
   List<HashWrapper> applicable=new ArrayList<>();long now=System.currentTimeMillis();for(int n=0;n<keys;n++){byte[] key=id(distant?240:1);key[5]=(byte)(n+1);key[18]=(byte)(n>>8);key[19]=(byte)n;HashWrapper hw=new HashWrapper(key);DHTDBMapping mapping=new DHTDBMapping(db,hw,false);mapping.add(new DHTDBValueImpl(now,new byte[]{(byte)n},n+1,db.destination,db.destination,false,0x21,72,(byte)replication));db.mappings().put(hw,mapping);db.addToPrefixMap(mapping);applicable.add(hw);keyInputs.add(key);}
   db.processSurvey(id(1),applicable,survey);int initial=pending.size(),maximum=initial;int guard=0;
   while(!pending.isEmpty()){
    if(++guard>100)throw new AssertionError("Original query callback loop");Pending request=pending.remove(0);if(response.equals("failed")){request.handler.failed(request.contact,new Exception("Generated failed query"));}
    else{List<byte[]> rows=new ArrayList<>();for(byte[] header:request.headers){byte[] result=null;if(response.equals("present")||(response.equals("one-present")&&(request.contact.getID()[0]&255)==2)){List<Object[]> groups=new ArrayList<>();groups.add(new Object[]{new byte[0],Arrays.asList(header)});result=db.queryStore(request.contact,6,groups).getEntries().get(0);}else if(response.equals("mismatch"))result=new byte[]{99,99};rows.add(result);}if(response.equals("short")&&!rows.isEmpty())rows.remove(rows.size()-1);request.handler.queryStoreReply(request.contact,rows);}
    maximum=Math.max(maximum,pending.size());
   }
   Map<String,Object> out=new TreeMap<>();out.put("name",name);out.put("k",(long)k);out.put("keys",keyInputs);out.put("contacts",contacts);out.put("local",id(1));out.put("include_local",includeLocal?1L:0L);out.put("replication",(long)replication);out.put("response",response);out.put("queries",queries);out.put("stores",stores);out.put("initial_pending",(long)initial);out.put("maximum_pending",(long)maximum);out.put("tokens",(long)tokenCount[0]);return out;
  }finally{db.destroy();}
 }
 public static void main(String[] args)throws Exception{
  List<Object> cases=new ArrayList<>();cases.add(run("missing-factor-three",20,2,3,"missing",true,false,false,false));cases.add(run("present-enough-copies",20,2,3,"present",true,false,false,false));cases.add(run("one-present-copy",20,2,3,"one-present",true,false,false,false));cases.add(run("mismatched-tail",20,2,3,"mismatch",true,false,false,false));cases.add(run("failed-query",20,2,3,"failed",true,false,false,false));cases.add(run("short-reply",20,2,3,"short",true,false,false,false));cases.add(run("factor-capped-by-k",2,2,15,"missing",true,false,false,false));cases.add(run("local-outside-k",2,2,3,"missing",true,true,false,false));cases.add(run("local-not-observed",20,2,3,"missing",false,false,false,false));cases.add(run("same-subnet-targets",20,2,3,"missing",true,false,true,false));cases.add(run("token-before-repair",20,2,3,"missing",true,false,false,true));cases.add(run("common-prefix-packing",20,40,3,"missing",true,false,false,false));cases.add(run("IPv6-subnet-is-not-filtered",20,2,3,"missing",true,false,false,false));cases.add(run("legacy-peer",20,2,3,"missing",true,false,false,false));cases.add(run("negative-protocol-peer",20,2,3,"missing",true,false,false,false));Map<String,Object> out=new TreeMap<>();out.put("cases",cases);Files.write(Paths.get(args[0]),BEncoder.encode(out));System.out.println("ORIGINAL_SURVEY_GRAPH cases="+cases.size());com.biglybt.core.security.SESecurityManager.exitVM(0);
 }
}
