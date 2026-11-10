package com.vuze.plugins.azmsgsync;
import java.io.File;import java.util.*;import com.biglybt.plugin.dht.*;
/** Test host only. All room, node, message and signature code is original. */
public final class MsgSyncPlugin {
 public static final int TIMER_PERIOD=2500;
 public void addGlobalBan(byte[] key){} public void removeGlobalBan(byte[] key){}
 public boolean isGlobalBan(MsgSyncMessage message){return false;}
 public Object getGlobalBans(){return Collections.emptyList();}
 public String getMessageText(String key,String[] arguments){return key;}
 public File getPersistDir(){return new File(System.getProperty("biglybtc.test.root"),"full-handler/save");}
 public void log(String message){}
 public MsgSyncHandler getSyncHandler(DHTPluginInterface dht,MsgSyncHandler parent,byte[] publicKey,Map<String,Object> contact,byte[] userKey,byte[] secret){throw new UnsupportedOperationException("Private rooms excluded from this owned test");}
}
