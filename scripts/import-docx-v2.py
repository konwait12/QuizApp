"""从docx导入题库到Project013 — 用已有题库做内容匹配"""
import os, json, re
from pathlib import Path
from docx import Document

DOWNLOADS = Path(r'd:/桌面/Workspace/浏览器下载')
DATA_DIR = Path(__file__).parent.parent / 'data'

def parse_docx(filepath):
    doc = Document(filepath)
    questions = []
    current = None
    for p in doc.paragraphs:
        text = p.text.strip()
        if not text: continue
        style = p.style.name if p.style else ''
        if 'Heading 3' in style:
            if current and current.get('q'): questions.append(current)
            current = {'q':'','options':[],'ans':'','type':'单选'}
            if '多选' in text: current['type']='多选'
            elif '判断' in text: current['type']='判断'
            elif '简答' in text: current['type']='简答'
        elif 'Heading 1' in style: pass
        elif '共' in text and '导出时间' in text: pass
        elif text.startswith('✓ 正确答案') or text.startswith('✓ 正确答案'):
            if current: current['ans'] = text.split(':',1)[-1].split('：',1)[-1].strip()
        elif current is not None:
            if re.match(r'^[A-Z][.、\s]', text):
                current['options'].append(text.strip())
            elif not current['q']: current['q'] = text
            elif current['options']: current['q'] += ' ' + text
    if current and current.get('q'): questions.append(current)
    return questions

# 1. 加载已有题库，建立章节→题干集合的映射
existing_chapters = {}
for f in sorted(DATA_DIR.glob('毛概-*.json')):
    with open(f, 'r', encoding='utf-8') as fp:
        data = json.load(fp)
    chapter = data.get('chapter', f.stem)
    existing_chapters[chapter] = set()
    for q in data.get('questions', []):
        stem = re.sub(r'\s+', '', q['q'])[:30]
        existing_chapters[chapter].add(stem)

print(f'已有章节: {list(existing_chapters.keys())}')

# 2. 解析所有docx并去重
all_qs = []
seen = set()
for f in sorted(DOWNLOADS.glob('quiz_export*.docx')):
    try:
        for q in parse_docx(f):
            key = re.sub(r'\s+', '', q['q'])[:40]
            if key not in seen:
                seen.add(key)
                q['id'] = f'q{len(all_qs)+1}'
                q['exp'] = ''
                all_qs.append(q)
    except Exception as e:
        print(f'Error: {f.name}: {e}')

print(f'去重后: {len(all_qs)} 题')

# 3. 对每道题，计算和已有章节的重叠度
from collections import defaultdict
chapter_qs = defaultdict(list)
unmatched = []

for q in all_qs:
    stem = re.sub(r'\s+', '', q['q'])[:30]
    best_chapter = None
    best_overlap = 0

    for ch, stems in existing_chapters.items():
        # 检查是否有相似的已有题目
        for s in stems:
            common = len(set(stem) & set(s))
            if common > best_overlap:
                # 更严格的匹配：开头字符相同
                if stem[:10] == s[:10]:
                    best_overlap = common * 10  # 给开头匹配更高权重
                elif stem[:5] == s[:5]:
                    best_overlap = common
        if best_overlap > 20:
            best_chapter = ch
            break

    if best_chapter:
        chapter_qs[best_chapter].append(q)
    else:
        unmatched.append(q)

print(f'\n匹配结果:')
for ch, qs in sorted(chapter_qs.items()):
    print(f'  {ch}: {len(qs)} 题')
print(f'  未匹配: {len(unmatched)} 题')

# 4. 保存
total_new = 0
for chapter, new_qs in chapter_qs.items():
    existing_file = None
    for f in DATA_DIR.glob(f'*{chapter}*.json'):
        existing_file = f; break

    if existing_file:
        with open(existing_file, 'r', encoding='utf-8') as f:
            data = json.load(f)
        exist_stems = {re.sub(r'\s+','',q['q'])[:40] for q in data['questions']}
        added = 0
        for q in new_qs:
            key = re.sub(r'\s+','',q['q'])[:40]
            if key not in exist_stems:
                q['id'] = f'q{len(data["questions"])+added+1}'
                data['questions'].append(q)
                exist_stems.add(key)
                added += 1
        with open(existing_file, 'w', encoding='utf-8') as f:
            json.dump(data, f, ensure_ascii=False, indent=2)
        print(f'更新: {existing_file.name} (+{added})')
        total_new += added

# 未匹配的保存
if unmatched:
    fp = DATA_DIR / '毛概-补充题库.json'
    data = {'name':'毛概-补充题库','subject':'毛概','chapter':'补充','questions':unmatched}
    with open(fp, 'w', encoding='utf-8') as f:
        json.dump(data, f, ensure_ascii=False, indent=2)
    print(f'补充题库: {fp} ({len(unmatched)}题)')

print(f'\n总计新增: {total_new} 题, 补充题库: {len(unmatched)} 题')
