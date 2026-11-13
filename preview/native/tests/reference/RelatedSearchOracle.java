// SPDX-License-Identifier: GPL-2.0-or-later
// Invoke original matching/normalisation methods against injected generated data.
// Unsafe avoids starting an original client, DHT, timer or subscription service.
import com.biglybt.core.config.impl.ConfigurationManager;
import com.biglybt.core.content.RelatedContent;
import com.biglybt.core.content.RelatedContentManager;
import com.biglybt.core.content.RelatedContentSearcher;
import com.biglybt.core.util.BEncoder;
import com.biglybt.core.util.Base32;
import com.biglybt.core.util.SystemTime;
import com.biglybt.core.util.bloom.BloomFilter;
import com.biglybt.core.util.bloom.BloomFilterFactory;
import com.biglybt.pif.ddb.DistributedDatabaseContact;
import java.lang.reflect.*;
import java.lang.ref.WeakReference;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.*;
public final class RelatedSearchOracle {
    private static Field field(Class<?> type, String name) throws Exception {
        Field field = type.getDeclaredField(name); field.setAccessible(true); return field;
    }
    private static Method method(Class<?> type, String name) throws Exception {
        for (Method method : type.getDeclaredMethods()) {
            if (method.getName().equals(name)) { method.setAccessible(true); return method; }
        }
        throw new IllegalArgumentException(name);
    }
    private static byte[] utf(String text) { return text.getBytes(StandardCharsets.UTF_8); }
    public static void main(String[] args) throws Exception {
        if (!(System.getSecurityManager() instanceof HybridResumeFence)) { throw new IllegalStateException("Generated oracle fence required"); }
        Locale.setDefault(Locale.US);
        Path root = Paths.get(System.getProperty("biglybtc.test.root")).toRealPath();
        ConfigurationManager.getInstance(new HashMap<String, Object>());
        Field access = field(sun.misc.Unsafe.class, "theUnsafe");
        sun.misc.Unsafe unsafe = (sun.misc.Unsafe)access.get(null);
        RelatedContentManager manager = (RelatedContentManager)unsafe.allocateInstance(RelatedContentManager.class);
        field(RelatedContentManager.class, "rcm_lock").set(manager, new Object());
        Class<?> cacheType = Class.forName("com.biglybt.core.content.RelatedContentManager$ContentCache");
        Constructor<?> constructor = cacheType.getDeclaredConstructor(); constructor.setAccessible(true);
        Object cache = constructor.newInstance();
        field(RelatedContentManager.class, "content_cache").set(manager, new WeakReference<Object>(cache));
        field(RelatedContentManager.class, "content_cache_ref").set(manager, cache);
        @SuppressWarnings("unchecked") Map<String, Object> contents = (Map<String, Object>)field(cacheType, "related_content").get(cache);
        Method decode = method(RelatedContentManager.class, "decodeInfo");
        Method tags = method(RelatedContentManager.class, "encodeTags");
        List<Map<String, Object>> items = new ArrayList<Map<String, Object>>();
        String[] special = {"alpha beta", "alpha -beta", "Generated plain", "Штучний Київ 🌍", "Искусственный пример", "Straße", "STRASSE", "ΣΙΓΜΑ σίγμα", "İstanbul", "istanbul", "測試 項目", "alpha\tbeta", "alpha\u00a0beta", "(alpha[)", ""};
        long now = SystemTime.getCurrentTime();
        for (int index = 0; index < 95; ++index) {
            String title = index < special.length ? special[index] : String.format(Locale.US, "generated alpha item %03d", index);
            byte[] hash = new byte[20]; Arrays.fill(hash, (byte)index); hash[0] = (byte)(index+1);
            Map<String, Object> wire = new HashMap<String, Object>();
            wire.put("d", utf(title)); wire.put("h", hash); wire.put("r", (long)index+1); wire.put("s", 1000L+index);
            wire.put("v", (long)(index%3)); wire.put("z", 10L+index); wire.put("l", (long)index);
            wire.put("p", index%5==0 ? 0L : now/3600000L-(index%2==0?48L:0L));
            String[] generatedTags = {"generated classics", "київ", index%2==0?"even":"odd"};
            wire.put("g", tags.invoke(manager, (Object)generatedTags));
            Object info = decode.invoke(manager, wire, null, 1, false, new HashSet<String>());
            if (info == null) { throw new AssertionError("Original generated item decode"); }
            field(info.getClass(), "rand_list").set(info, new int[index+1]);
            contents.put(Base32.encode(hash), info);
            Map<String, Object> row = new HashMap<String, Object>(); row.put("wire", wire); row.put("rank", (long)index+1); items.add(row);
        }
        RelatedContentSearcher searcher = (RelatedContentSearcher)unsafe.allocateInstance(RelatedContentSearcher.class);
        field(RelatedContentSearcher.class, "manager").set(searcher, manager);
        field(RelatedContentSearcher.class,"ignore_words").set(searcher,new HashSet<String>(Arrays.asList("a","in","of","at","the","and","or","if","to","an","for","with")));
        Method wordsMethod=method(RelatedContentSearcher.class,"getDHTWords");
        BloomFilter bloom=BloomFilterFactory.createAddOnly(50000);
        for(Map<String,Object> row:items){
            @SuppressWarnings("unchecked") Map<String,Object> wire=(Map<String,Object>)row.get("wire");
            Object info=contents.get(Base32.encode((byte[])wire.get("h")));
            @SuppressWarnings("unchecked") List<String> words=(List<String>)wordsMethod.invoke(searcher,info);
            List<byte[]> encodedWords=new ArrayList<byte[]>();for(String word:words){byte[] value=utf(word);encodedWords.add(value);bloom.add(value);}row.put("words",encodedWords);
        }
        Class<?> foreignType=Class.forName("com.biglybt.core.content.RelatedContentSearcher$ForeignBloom");
        Constructor<?> foreignConstructor=foreignType.getDeclaredConstructor(DistributedDatabaseContact.class,BloomFilter.class);foreignConstructor.setAccessible(true);
        com.biglybt.core.util.ByteArrayHashMap<Object> harvested=new com.biglybt.core.util.ByteArrayHashMap<Object>();harvested.put(utf("generated"),foreignConstructor.newInstance(null,bloom));
        field(RelatedContentSearcher.class,"harvested_blooms").set(searcher,harvested);
        Method bloomMatch=method(RelatedContentSearcher.class,"searchForeignBlooms");
        Method normalise = method(RelatedContentSearcher.class, "fixupTerm");
        Method match = method(RelatedContentSearcher.class, "matchContent0");
        String[] terms = {"alpha", "alpha beta", "alpha -beta", "-beta", "+alpha", "alpha | beta", "tag:generated+classics", "tag:київ", "tag:even|beta", "\"alpha beta\"", "\" alpha  beta \"", "\"alpha\tbeta\"", "(alpha.*beta)", "(alpha|beta)", "(alpha[)", "((a+)+)", "ИСКУССТВЕННЫЙ", "КИЇВ", "STRASSE", "straße", "ΣΙΓΜΑ", "İSTANBUL", "🌍", "測試", "", "   ", "\t", "alpha\tbeta", "alpha\u00a0beta", "(.)", "hash:"+contents.keySet().iterator().next(), "hash:"+contents.keySet().iterator().next().toLowerCase(Locale.US), "-tag:even", "tag:gen", "tag:ODD", "(generated.*?[0-9]+)", "\"\"", "\" \""};
        List<Map<String, Object>> queries = new ArrayList<Map<String, Object>>();
        for (int index = 0; index < terms.length+4; ++index) {
            String term = index<terms.length?terms[index]:"generated";
            int minimumSeeds = index==terms.length?50:-1, minimumLeechers = index==terms.length+1?50:-1;
            int age = index==terms.length+2?3600:index==terms.length+3?3*86400:-1;
            String fixed = (String)normalise.invoke(searcher, term);
            @SuppressWarnings("unchecked") List<RelatedContent> matched = (List<RelatedContent>)match.invoke(searcher, fixed, minimumSeeds, minimumLeechers, age, true);
            List<byte[]> hashes = new ArrayList<byte[]>(); for (RelatedContent item:matched) { hashes.add(item.getHash()); }
            Map<String, Object> query = new HashMap<String, Object>(); query.put("term", utf(term)); query.put("normalised", utf(fixed)); query.put("s", (long)minimumSeeds); query.put("l", (long)minimumLeechers); query.put("a", (long)age); query.put("expected", hashes);
            Map<String,Object> request=new HashMap<String,Object>();request.put("t",fixed);if(minimumSeeds>0)request.put("s",(long)minimumSeeds);if(minimumLeechers>0)request.put("l",(long)minimumLeechers);if(age>0)request.put("a",(long)age);query.put("requestWire",BEncoder.encode(request));
            query.put("bloom", ((List<?>)bloomMatch.invoke(searcher,fixed)).isEmpty()?0L:1L); queries.add(query);
        }
        Map<String, Object> result = new HashMap<String, Object>(); result.put("items", items); result.put("queries", queries); result.put("now", now); result.put("bloom",bloom.serialiseToMap());BloomFilter boundary=BloomFilterFactory.createAddOnly(50000);for(int i=0;boundary.getEntryCount()<6251&&i<20000;i++){boundary.add(utf("generated-boundary-"+i));}if(boundary.getEntryCount()!=6251)throw new AssertionError("Original one-word DHT tail boundary");result.put("boundaryBloom",boundary.serialiseToMap());
        List<byte[]> handlers=new ArrayList<byte[]>();
        Class<?> helpers=Class.forName("com.biglybt.pifimpl.local.ddb.DDBaseHelpers");Method handlerKey=method(helpers,"getKey");for(String name:new String[]{"RCMSearchXFer","RCMSearchXFerBiglyBT"}){Class<?> type=Class.forName("com.biglybt.core.content.RelatedContentManager$"+name);Constructor<?> c=type.getDeclaredConstructor();c.setAccessible(true);Object key=handlerKey.invoke(null,c.newInstance());handlers.add((byte[])key.getClass().getMethod("getHash").invoke(key));}result.put("handlers",handlers);
        List<Map<String,Object>> responseItems=new ArrayList<Map<String,Object>>();for(int index=0;index<30;index++){@SuppressWarnings("unchecked") Map<String,Object> original=(Map<String,Object>)items.get(index).get("wire");Map<String,Object> row=new HashMap<String,Object>(original);row.put("n",row.remove("d"));row.put("r",items.get(index).get("rank"));row.put("d",now/1000);responseItems.add(row);}Map<String,Object> response=new HashMap<String,Object>();response.put("l",responseItems);result.put("responseWire",BEncoder.encode(response));
        Files.write(root.resolve("original-search-fixtures.ben"), BEncoder.encode(result));
        System.out.println("PASS_ORIGINAL_RELATED_SEARCH items="+items.size()+" queries="+queries.size());
        System.exit(0);
    }
}
