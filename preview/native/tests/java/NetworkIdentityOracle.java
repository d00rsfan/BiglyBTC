// SPDX-License-Identifier: GPL-2.0-or-later
// Generated data only; runs pinned original DNS parser without any DNS/network call.
import com.biglybt.core.networkmanager.admin.impl.NetworkAdminASNLookupImpl;
import com.biglybt.core.networkmanager.admin.impl.NetworkAdminASNImpl;
import com.biglybt.core.networkmanager.admin.NetworkAdminException;
import com.biglybt.core.util.BEncoder;
import java.net.InetAddress;
import java.nio.file.*;
import java.util.*;
public final class NetworkIdentityOracle extends NetworkAdminASNLookupImpl {
 String query,origin,nameRecord;boolean ipv6;
 NetworkIdentityOracle(InetAddress address){super(address);ipv6=address.getAddress().length==16;}
 @Override protected String lookupDNS(String name)throws NetworkAdminException {
  if(name.startsWith("AS")){nameRecord="64500 | ZZ | test | 2000-01-01 | Generated мережа";return nameRecord;}
  query=name;origin="64500 | "+(ipv6?"2606:4700::/32":"8.8.8.0/24")+" | ZZ | test | 2000-01-01";return origin;
 }
 public static void main(String[] args) {try{
  List<Map<String,Object>> rows=new ArrayList<>();for(String ip:new String[]{"8.8.8.8","2606:4700::1111"}){InetAddress address=InetAddress.getByName(ip);NetworkIdentityOracle oracle=new NetworkIdentityOracle(address);NetworkAdminASNImpl result=oracle.lookupDNS(address);Map<String,Object> row=new HashMap<>();row.put("ip",ip);row.put("query",oracle.query);row.put("origin",oracle.origin);row.put("as",result.getAS());row.put("bgp",result.getBGPPrefix());row.put("name",result.getASName());row.put("name_record",oracle.nameRecord);rows.add(row);}Map<String,Object> root=new HashMap<>();root.put("rows",rows);Files.write(Path.of(args[0],"oracle.ben"),BEncoder.encode(root));System.out.println("PASS_ORIGINAL_NETWORK_IDENTITY_ORACLE");
 }catch(Exception failure){throw new RuntimeException(failure);}
 }
}
