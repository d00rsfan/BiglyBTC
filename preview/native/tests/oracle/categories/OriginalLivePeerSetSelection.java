// SPDX-License-Identifier: GPL-2.0-or-later
package com.biglybt.core.speedmanager;

import com.biglybt.core.config.COConfigurationManager;
import com.biglybt.core.security.SESecurityManager;
import com.biglybt.core.util.BEncoder;
import com.biglybt.pif.PluginInterface;
import com.biglybt.pif.download.Download;
import com.biglybt.pif.network.ConnectionManager;
import com.biglybt.pif.network.RateLimiter;
import com.biglybt.pif.peers.Peer;
import com.biglybt.pif.peers.PeerManager;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.regex.Pattern;

public final class OriginalLivePeerSetSelection {
    public static void main(String[] args) throws Exception {
        COConfigurationManager.initialiseFromMap(new HashMap<>());
        COConfigurationManager.setParameter("tagmanager.enable",false);
        Object rate=OriginalPeerSetClassifier.proxy(RateLimiter.class,
            (p,m,a)->OriginalPeerSetClassifier.primitive(m.getReturnType()));
        Object factory=OriginalPeerSetClassifier.proxy(ConnectionManager.class,
            (p,m,a)->m.getName().equals("createRateLimiter") ? rate : OriginalPeerSetClassifier.primitive(m.getReturnType()));
        Object plugin=OriginalPeerSetClassifier.proxy(PluginInterface.class,
            (p,m,a)->m.getName().equals("getConnectionManager") ? factory : OriginalPeerSetClassifier.primitive(m.getReturnType()));
        var outer=(SpeedLimitHandler)OriginalPeerSetClassifier.U.allocateInstance(SpeedLimitHandler.class);
        OriginalPeerSetClassifier.set(outer,SpeedLimitHandler.class,"plugin_interface",plugin);
        var type=Class.forName(SpeedLimitHandler.class.getName()+"$PeerSet");
        var ctor=type.getDeclaredConstructor(SpeedLimitHandler.class,String.class);
        var add=type.getDeclaredMethod("addCIDRorCCetc",String.class);
        var params=type.getDeclaredMethod("setParameters",boolean.class,int.class,int.class,int.class,int.class,
            Set.class,Pattern.class,boolean.class,Pattern.class,boolean.class,Pattern.class,boolean.class,String.class);
        var attach=SpeedLimitHandler.class.getDeclaredMethod("peersAdded",Download.class,PeerManager.class,Peer[].class);
        ctor.setAccessible(true);add.setAccessible(true);params.setAccessible(true);attach.setAccessible(true);
        String[] definitions={"203.0.113.0/24","Public","IPv4","IPv6","I2P","Tor","all","IPv4;IPv6",
            "203.0.113.0/24;IPv6","203.0.113.0/24;Public","203.0.113.0/24;I2P","203.0.113.0/24;Tor"};
        String[] addresses={"203.0.113.1","198.51.100.1","127.0.0.1","0.0.0.0","2001:db8::1","::1","owned.i2p","owned.onion"};
        var rows=new ArrayList<Map<String,Object>>();
        for(String definition:definitions) for(boolean inverse:new boolean[]{false,true}) {
            var set=ctor.newInstance(outer,"Generated live selector");
            for(String token:definition.split(";")) if(!(boolean)add.invoke(set,token)) throw new AssertionError(token);
            params.invoke(set,inverse,-1,-1,0,0,Set.of(),null,false,null,false,null,false,null);
            OriginalPeerSetClassifier.set(outer,SpeedLimitHandler.class,"current_ip_sets",Map.of("Generated live selector",set));
            for(String address:addresses) {
                var data=new HashMap<Object,Object>();
                var installed=new boolean[2];
                Peer peer=(Peer)OriginalPeerSetClassifier.proxy(Peer.class,(p,m,a)->switch(m.getName()) {
                    case "getIp" -> address;
                    case "getUserData" -> data.get(a[0]);
                    case "setUserData" -> {data.put(a[0],a[1]); yield null;}
                    case "getRateLimiters" -> new RateLimiter[0];
                    case "addRateLimiter" -> {int direction=(boolean)a[1]?0:1;
                        if(installed[direction]) throw new AssertionError("Duplicate limiter for "+address);
                        installed[direction]=true; yield null;}
                    default -> OriginalPeerSetClassifier.primitive(m.getReturnType());
                });
                attach.invoke(outer,null,null,(Object)new Peer[]{peer});
                if(installed[0]!=installed[1]) throw new AssertionError("Direction mismatch");
                rows.add(Map.of("definition",definition,"inverse",inverse?1L:0L,
                    "address",address,"match",installed[0]?1L:0L));
            }
        }
        Files.write(Path.of(args[0]),BEncoder.encode(Map.of("rows",rows)));
        System.out.println("PASS "+rows.size()+" original SpeedLimitHandler.peersAdded CIDR and network membership evaluations");
        SESecurityManager.exitVM(0);
    }
}
