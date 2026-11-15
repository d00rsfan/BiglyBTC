// SPDX-License-Identifier: GPL-2.0-or-later
package com.biglybt.core.dht.db.impl;
import com.biglybt.core.config.COConfigurationManager;
import com.biglybt.core.dht.*;
import com.biglybt.core.util.*;
import com.biglybt.core.versioncheck.VersionCheckClient;
import java.lang.reflect.*;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;

/** Actual saved VCC getter and DB constructor gate, no initialise/network call. */
public final class OriginalSurveyPolicy {
 static final List<Object> cases=new ArrayList<>();
 static byte[] bytes(String s){return s==null?null:s.getBytes(StandardCharsets.UTF_8);}
 static void set(VersionCheckClient client,String name,Object value)throws Exception{Field f=VersionCheckClient.class.getDeclaredField(name);f.setAccessible(true);f.set(client,value);}
 static void run(String name,boolean v4,byte[] flag4,boolean v6,byte[] flag6)throws Exception{
  Map<String,Object> config=new TreeMap<>();COConfigurationManager.removeParameter("versioncheck.cache.v4");COConfigurationManager.removeParameter("versioncheck.cache.v6");
  for(int family:new int[]{4,6}){if(family==4?v4:v6){Map<String,Object> cache=new TreeMap<>();byte[] flag=family==4?flag4:flag6;if(flag!=null)cache.put("feat_flags",flag);String key="versioncheck.cache.v"+family;config.put(key,cache);COConfigurationManager.setParameter(key,cache);}}
  VersionCheckClient client=VersionCheckClient.getSingleton();set(client,"last_check_data_v4",null);set(client,"last_check_data_v6",null);set(client,"last_feature_flag_cache_time",0L);set(client,"last_feature_flag_cache",0L);long flags=client.getFeatureFlags();boolean feature=FeatureAvailability.isDHTRepV2Enabled();List<Object> gates=new ArrayList<>();
  for(int protocol:new int[]{25,26,55})for(int network:new int[]{0,1,3,4})for(boolean hasAdapter:new boolean[]{false,true}){
   DHTStorageAdapter adapter=hasAdapter?OriginalCacheRepublish.proxy(DHTStorageAdapter.class,(p,m,a)->m.getName().equals("getNetwork")?network:OriginalCacheRepublish.defaults(m)):null;
   DHTDBImpl db=new DHTDBImpl(adapter,8*3600000,1800000,(byte)protocol,OriginalCacheRepublish.logger());try{Field f=DHTDBImpl.class.getDeclaredField("survey_timer");f.setAccessible(true);Map<String,Object> gate=new TreeMap<>();gate.put("protocol",(long)protocol);gate.put("network",(long)network);gate.put("adapter",hasAdapter?1L:0L);gate.put("enabled",f.get(db)!=null?1L:0L);gates.add(gate);}finally{db.destroy();}
  }
  Map<String,Object> row=new TreeMap<>();row.put("name",name);row.put("config",config);row.put("flags",flags);row.put("feature",feature?1L:0L);row.put("gates",gates);cases.add(row);
 }
 public static void main(String[] args)throws Exception{
  run("absent",false,null,false,null);run("v6-fallback",false,null,true,bytes("32"));run("empty-v4-wins",true,null,true,bytes("32"));run("v4-zero-wins",true,bytes("0"),true,bytes("32"));run("v4-disabled-wins",true,bytes("32"),true,bytes("0"));run("bad-v4-does-not-fall-through",true,bytes("bad"),true,bytes("32"));
  for(String value:new String[]{"+32","00032","-1","9223372036854775807","-9223372036854775808","9223372036854775808","-9223372036854775809"," 32","32 ","","+","٣٢","３２","𝟛𝟚","\uFEFF32","\u000032"})run("numeric-"+cases.size(),true,bytes(value),false,null);
  run("invalid-utf8",true,new byte[]{(byte)255},false,null);Map<String,Object> out=new TreeMap<>();out.put("cases",cases);Files.write(Paths.get(args[0]),BEncoder.encode(out));System.out.println("ORIGINAL_SURVEY_POLICY cases="+cases.size()+" constructor_gates="+(cases.size()*24));com.biglybt.core.security.SESecurityManager.exitVM(0);
 }
}
