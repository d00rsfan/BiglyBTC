# SPDX-License-Identifier: GPL-2.0-or-later
from pathlib import Path
import subprocess,re,json,hashlib
r=Path(__import__('sys').argv[1]).resolve();w=Path(__import__('sys').argv[2]).resolve();w.mkdir(parents=True,exist_ok=True)
path='uis/src/com/biglybt/ui/swt/shells/opentorrent/OpenTorrentOptionsWindow.java';baseline='cb8c6cb9bb97916cbc7e80338a017200ffc3e71f';raw=subprocess.check_output(['git','show',baseline+':'+path],cwd=r);text=raw.decode();m=re.search(r'class\s+TagTypeDiscovery\s+extends\s+TagTypeBase\s*\{',text);assert m;start=m.end()-1;level=0;end=None
for at in range(start,len(text)):
 if text[at]=='{':level+=1
 elif text[at]=='}':
  level-=1
  if level==0:end=at+1;break
assert end;body=text[start:end];(w/'PinnedSuggestionTagType.java').write_text('package com.biglybt.core.tag.impl;\nimport java.util.*;\nimport com.biglybt.core.tag.*;\nimport com.biglybt.core.internat.MessageText;\nfinal class PinnedSuggestionTagType extends TagTypeBase '+body.replace('TagTypeDiscovery()', 'PinnedSuggestionTagType()')+'\n')
(w/'source-attestation.json').write_text(json.dumps({'original_commit':baseline,'source_path':path,'source_sha256':hashlib.sha256(raw).hexdigest(),'extracted_class_body_sha256':hashlib.sha256(body.encode()).hexdigest(),'adaptation':'extract exact nested body; rename class/constructor only; Core imports supplied; no SWT/UI qualification'},indent=2)+'\n')
print("Pinned suggestion class body extracted;see source-attestation.json")
