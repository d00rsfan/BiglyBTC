// SPDX-License-Identifier: GPL-2.0-or-later
package com.biglybt.core.tag.impl;
import com.biglybt.core.download.*;
import com.biglybt.core.tag.*;
import com.biglybt.core.util.*;
import java.lang.reflect.*;
import java.nio.file.*;
import java.util.*;
import java.util.concurrent.atomic.AtomicInteger;
import sun.misc.Unsafe;
/** Actual constraint constructor/compiler/checkStuff/application, recording tag effects only. */
public final class OriginalNewOnlyDependencyGraphs {
 static final Unsafe U;static final Class<?> CONSTRAINT,EXPR;
 static{try{Field f=Unsafe.class.getDeclaredField("theUnsafe");f.setAccessible(true);U=(Unsafe)f.get(null);CONSTRAINT=Class.forName("com.biglybt.core.tag.impl.TagPropertyConstraintHandler$TagConstraint");EXPR=Class.forName(CONSTRAINT.getName()+"$ConstraintExpr");}catch(Exception e){throw new RuntimeException(e);}}
 static void set(Object o,Class<?> owner,String name,Object value)throws Exception{Field f=owner.getDeclaredField(name);f.setAccessible(true);f.set(o,value);}
 static final class RecordingTag extends TagDownloadWithState {
  int id;CopyOnWriteSet<Taggable> members;List<Object> trace;
  RecordingTag(){super(null,0,"generated",false,false,false,false,0);}
  @Override public int getTagID(){return id;}
  @Override public String getTagName(boolean localized){return "Generated-"+id;}
  @Override public void addTaggable(Taggable value){trace.add("add:"+id);members.add(value);}
  @Override public void removeTaggable(Taggable value){trace.add("remove:"+id);members.remove(value);}
  @Override public void setColors(long[] values){trace.add("positive-evaluation:"+id);}
  @Override public boolean isAnyActionEnabled(){return false;}
  @Override public int getMaximumTaggables(){return 0;}
 }
 static Map<String,Object> node(int mode,boolean enabled,boolean member,int expression,int testMask,int... deps){Map<String,Object> n=new TreeMap<>();n.put("mode",(long)mode);n.put("enabled",enabled?1L:0L);n.put("member",member?1L:0L);n.put("expression",(long)expression);n.put("test_mask",(long)testMask);List<Object> edges=new ArrayList<>();for(int id:deps)edges.add((long)id);n.put("dependencies",edges);return n;}
 @SuppressWarnings("unchecked") static Map<String,Object> run(String name,List<Map<String,Object>> input,boolean check,boolean fresh,int prechecked,int recent,int stopAfter)throws Exception{
  TagManagerImpl manager=(TagManagerImpl)U.allocateInstance(TagManagerImpl.class);set(null,TagManagerImpl.class,"singleton",manager);
  TagTypeDownloadManual type=(TagTypeDownloadManual)U.allocateInstance(TagTypeDownloadManual.class);set(type,TagTypeBase.class,"tag_type",3);set(type,TagTypeBase.class,"tag_type_features",TagFeature.TF_PROPERTIES);set(type,TagTypeBase.class,"tags_or_tag_membership_mut",new AtomicInteger());set(type,TagTypeBase.class,"TTP_TAGS_FOR_TAGGABLE_CACHE","generated-cache");CopyOnWriteList<Tag> registered=new CopyOnWriteList<>();set(type,TagTypeWithState.class,"tags",registered);CopyOnWriteList<TagTypeBase> types=new CopyOnWriteList<>();types.add(type);set(manager,TagManagerImpl.class,"tag_types",types);
  Object handler=U.allocateInstance(TagPropertyConstraintHandler.class);Map<Tag,Object> constraints=new HashMap<>();Map<Tag,Map<DownloadManager,Long>> history=new HashMap<>();set(handler,TagPropertyConstraintHandler.class,"tag_manager",manager);set(handler,TagPropertyConstraintHandler.class,"constrained_tags",constraints);set(handler,TagPropertyConstraintHandler.class,"apply_history",history);
  DownloadManager dm=(DownloadManager)Proxy.newProxyInstance(OriginalNewOnlyDependencyGraphs.class.getClassLoader(),new Class[]{DownloadManager.class},(p,m,a)->{switch(m.getName()){case "getTaggableType":return Taggable.TT_DOWNLOAD;case "getTagMutationCount":return 0L;case "getTaggableTransientProperty":return null;case "setTaggableTransientProperty":return null;case "hashCode":return System.identityHashCode(p);case "equals":return p==a[0];case "getDisplayName":return "Generated";default:return null;}});
  List<Object> trace=new ArrayList<>();RecordingTag[] tags=new RecordingTag[input.size()];Object[] rules=new Object[input.size()];int[] evaluations={0};
  for(int i=0;i<input.size();i++){RecordingTag t=(RecordingTag)U.allocateInstance(RecordingTag.class);t.id=i;t.members=new CopyOnWriteSet<>(true);set(t,TagWithState.class,"objects",t.members);t.trace=trace;if(((Number)input.get(i).get("member")).longValue()!=0)t.members.add(dm);set(t,TagBase.class,"tag_id",i);set(t,TagBase.class,"tag_type",type);registered.add(t);tags[i]=t;}
  Constructor<?> ctor=CONSTRAINT.getDeclaredConstructor(TagPropertyConstraintHandler.class,Tag.class,String.class,String.class,boolean.class);ctor.setAccessible(true);
  for(int i=0;i<input.size();i++){Map<String,Object> n=input.get(i);int mode=((Number)n.get("mode")).intValue(),expression=((Number)n.get("expression")).intValue();
   String source=expression==4?"1==1||hasTag(\"Generated-"+((Number)((List<?>)n.get("dependencies")).get(0)).intValue()+"\")":expression==2?"hasTag(\"Generated-1\")":expression==1?"1==1":"1==2";
   rules[i]=ctor.newInstance(handler,tags[i],source,"am="+mode+";",((Number)n.get("enabled")).longValue()!=0);constraints.put(tags[i],rules[i]);
   Field error=CONSTRAINT.getDeclaredField("error");error.setAccessible(true);if(error.get(rules[i])!=null)throw new AssertionError("Original compile failed: "+error.get(rules[i]));
  }
  if(recent>=0){Map<DownloadManager,Long> seen=new IdentityHashMap<>();seen.put(dm,SystemTime.getMonotonousTime()+60000);history.put(tags[recent],seen);}
  Set<Object> checked=null;if(prechecked>=0){checked=new HashSet<>();checked.add(rules[prechecked]);}
  Method apply=CONSTRAINT.getDeclaredMethod("applySupport",Set.class,DownloadManager.class,boolean.class);apply.setAccessible(true);apply.invoke(rules[0],tags[0].getTagged(),dm,fresh);
  List<Object> members=new ArrayList<>();for(RecordingTag tag:tags)members.add(tag.members.contains(dm)?1L:0L);List<Object> checkedAfter=new ArrayList<>();for(int i=0;i<rules.length;i++)if(checked!=null&&checked.contains(rules[i]))checkedAfter.add((long)i);
  Map<String,Object> result=new TreeMap<>();Field checkField=CONSTRAINT.getDeclaredField("must_check_dependencies");checkField.setAccessible(true);
  result.put("root_dependency_check",checkField.getBoolean(rules[0])?1L:0L);result.put("name",name);result.put("nodes",input);result.put("check",check?1L:0L);result.put("fresh",fresh?1L:0L);result.put("prechecked",(long)prechecked);result.put("recent",(long)recent);result.put("stop_after",(long)stopAfter);result.put("trace",trace);result.put("members",members);result.put("checked_after",checkedAfter);return result;
 }
 public static void main(String[] args)throws Exception{List<Object> cases=new ArrayList<>();
  cases.add(run("new-only-chain",Arrays.asList(node(3,true,false,4,0,1),node(3,true,false,1,0)),true,true,-1,-1,0));
  cases.add(run("new-only-cycle",Arrays.asList(node(3,true,false,4,0,1),node(3,true,false,4,0,0)),true,true,-1,-1,0));
  cases.add(run("new-only-disabled-child",Arrays.asList(node(3,true,false,4,0,1),node(3,false,false,1,0)),true,true,-1,-1,0));
  cases.add(run("new-only-false-child",Arrays.asList(node(3,true,false,4,0,1),node(3,true,false,0,0)),true,true,-1,-1,0));
  cases.add(run("old-no-new-only-recursion",Arrays.asList(node(3,true,false,4,0,1),node(3,true,false,1,0)),true,false,-1,-1,0));
  Map<String,Object> out=new TreeMap<>();out.put("cases",cases);Files.write(Paths.get(args[0]),BEncoder.encode(out));System.out.println("ORIGINAL_NEW_ONLY_DEPENDENCY_GRAPHS cases="+cases.size()+" actual_constructor_compiler_and_applySupport=1 PASS");com.biglybt.core.security.SESecurityManager.exitVM(0);
 }
}
