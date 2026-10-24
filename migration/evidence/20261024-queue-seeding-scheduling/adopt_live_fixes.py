"""One-time hash-checked application of runtime fixes and original config probe."""
import hashlib,json,shutil
from pathlib import Path
w=Path(__file__).resolve().parent;n=Path('/home/yurii/Java/BiglyBTC')
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
changes={}
for name in ['scheduler-live-fix','dialog-live-fix']:
 d=w/name;m=json.loads((d/'manifest.json').read_text())
 if 'sources' in m:
  m['source_sha256']={p:v['after_sha256'] for p,v in m['sources'].items()};m['before_sha256']={p:v['before_sha256'] for p,v in m['sources'].items()}
 for path,digest in m['source_sha256'].items():
  assert sha(n/path)==m['before_sha256'][path],path
  assert sha(d/'staging'/path)==digest,path
  changes[path]=(d/'staging'/path,m['before_sha256'][path],digest)
p='preview/native/tests/managed_seeding_tests.cpp';m=json.loads((w/'seeding-test-race.json').read_text());assert sha(n/p)==m['before'];assert sha(w/'root/staging'/p)==m['after'];changes[p]=(w/'root/staging'/p,m['before'],m['after'])
p='preview/native/tests/queue_settings_roundtrip_driver.cpp';assert not (n/p).exists();m=json.loads((w/'queue-config/manifest.json').read_text());assert sha(w/'queue-config/driver.cpp')==m['files']['driver.cpp']['sha256'];changes[p]=(w/'queue-config/driver.cpp',None,sha(w/'queue-config/driver.cpp'))
p='preview/native/tests/CMakeLists.txt';before=sha(n/p);text=(n/p).read_text();text+='\n'+(w/'queue-config/cmake-target.proposed').read_text();staged=w/'root/staging'/p;staged.write_text(text);changes[p]=(staged,before,sha(staged))
p='preview/native/app/session_bridge.cpp';before=sha(n/p);text='\n'.join(line.rstrip() for line in (n/p).read_text().splitlines())+'\n';staged=w/'root/staging'/p;staged.write_text(text);changes[p]=(staged,before,sha(staged))
p='preview/native/tests/options_dialog_tests.cpp';before=sha(n/p);text=(n/p).read_text();text=text.replace('#include <QTreeWidget>','#include <QTreeWidget>\n#include <QTreeWidgetItemIterator>')
needle='      QVERIFY(h.dialog.grab().save(capture + QStringLiteral("/options-proxy.png")));'
assert needle in text
text=text.replace(needle,needle+'''

      tree->expandAll();
      for (const auto& name : {QStringLiteral("Queue"), QStringLiteral("Seeding"), QStringLiteral("First Priority"), QStringLiteral("Ignore")}) {
        QTreeWidgetItem* page = nullptr;
        for (QTreeWidgetItemIterator it(tree); *it; ++it) { if ((*it)->text(0) == name) { page = *it; break; } }
        QVERIFY(page); tree->setCurrentItem(page); QCoreApplication::processEvents();
        QVERIFY(h.dialog.grab().save(capture + QStringLiteral("/options-") + name.toLower().replace(' ', '-') + QStringLiteral(".png")));
      }''')
staged=w/'root/staging'/p;staged.write_text(text);changes[p]=(staged,before,sha(staged))
record={'changes':{p:{'before':a,'after':b} for p,(_,a,b) in changes.items()},'capsules':{name:sha(w/name/'manifest.json') for name in ['scheduler-live-fix','dialog-live-fix','queue-config']},'reason':'Actual GCC failures; preserve failure evidence. No redundant descriptor amendment.'}
out=w/'live-fixes-adoption.json';assert not out.exists()
for p,(source,old,new) in changes.items():
 (n/p).parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,n/p);assert sha(n/p)==new
out.write_text(json.dumps(record,indent=2)+'\n');print('Adopted',len(changes),'files')
