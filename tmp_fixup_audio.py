import os
import re
import glob

dirs_to_scan = [
    r"C:\AC6Recomp_ext\thirdparty\rexglue-sdk\include\native\audio",
    r"C:\AC6Recomp_ext\thirdparty\rexglue-sdk\src\native\audio"
]

xenia_regex = re.compile(r'/\*\*\s*\*{50,}.*?\*/\s*', re.DOTALL)

for d in dirs_to_scan:
    for root, _, files in os.walk(d):
        for f in files:
            if f.endswith('.h') or f.endswith('.cpp') or f.endswith('.inc'):
                filepath = os.path.join(root, f)
                with open(filepath, 'r', encoding='utf-8') as file:
                    content = file.read()
                
                orig = content
                
                # Replace Xenia header
                content = xenia_regex.sub("// Native audio runtime\n// Part of the AC6 Recompilation native foundation\n\n", content)
                
                # Replace includes
                content = content.replace("<rex/audio/", "<native/audio/")
                content = content.replace("\"rex/audio/", "\"native/audio/")
                content = content.replace("<rex/assert.h>", "<native/assert.h>")
                content = content.replace("<rex/memory/utils.h>", "<native/memory/utils.h>")
                content = content.replace("<rex/math.h>", "<native/math.h>")
                content = content.replace("<rex/thread.h>", "<native/thread.h>")
                content = content.replace("<rex/thread/mutex.h>", "<native/thread/mutex.h>")
                content = content.replace("<rex/string.h>", "<native/string.h>")
                
                if content != orig:
                    with open(filepath, 'w', encoding='utf-8') as file:
                        file.write(content)
                    print(f"Updated: {f}")

print("Done")
