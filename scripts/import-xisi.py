"""从docx导入新科目'习思'到Project013"""
import os, json, re
from pathlib import Path
from collections import Counter
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
        elif '导出时间' in text: pass
        elif text.startswith('✓ 正确答案'):
            if current: current['ans'] = re.split('[：:]', text, 1)[-1].strip()
        elif current is not None:
            if re.match(r'^[A-Z][.、\s]', text):
                current['options'].append(text.strip())
            elif not current['q']: current['q'] = text
            elif current['options']: current['q'] += ' ' + text
    if current and current.get('q'): questions.append(current)
    return questions

# 解析每个docx，按文件分组
file_groups = {}
for f in sorted(DOWNLOADS.glob('quiz_export*.docx')):
    try:
        qs = parse_docx(f)
        if qs:
            # 用题干前几个词命名组
            first_stems = ' '.join([q['q'][:20] for q in qs[:3]])
            file_groups[f.name] = qs
    except Exception as e:
        print(f'Error {f.name}: {e}')

print(f'文件组: {len(file_groups)}')

# 去重所有题
seen = set()
all_qs = []
for fname, qs in file_groups.items():
    for q in qs:
        key = re.sub(r'\s+','',q['q'])[:50]
        if key not in seen:
            seen.add(key)
            q['id']=f'q{len(all_qs)+1}'
            q['exp']=''
            all_qs.append(q)

print(f'去重: {len(all_qs)} 题')

# 按内容自动分章节（用常见关键词）
chapters_def = {
    '导论': ['新时代','时代课题','历史方位','主要矛盾'],
    '中国特色社会主义': ['中国特色社会主义','总任务','中国梦','本质','必由之路','制度优势'],
    '党的领导': ['党的领导','全面从严治党','政治建设','自我革命'],
    '经济建设': ['经济','高质量发展','新发展理念','现代化经济','供给侧'],
    '政治建设': ['政治制度','人民当家作主','协商民主','法治'],
    '文化建设': ['文化','意识形态','核心价值观','文化自信'],
    '社会建设': ['民生','社会治理','共同富裕','保障'],
    '生态文明': ['生态','绿色','人与自然','美丽中国'],
    '国家安全': ['国家安全','国防','军队','强军'],
    '外交': ['外交','人类命运共同体','一带一路','和平发展'],
}

chapter_qs = {ch:[] for ch in chapters_def}
unmatched = []

for q in all_qs:
    best, best_ch = 0, None
    for ch, kws in chapters_def.items():
        score = sum(1 for kw in kws if kw in q['q'])
        if score > best:
            best = score
            best_ch = ch
    if best > 0:
        chapter_qs[best_ch].append(q)
    else:
        unmatched.append(q)

# 打印结果
for ch, qs in chapter_qs.items():
    print(f'  {ch}: {len(qs)} 题')
print(f'  未分类: {len(unmatched)} 题')

# 清除旧习思数据
for f in DATA_DIR.glob('习思*.json'):
    os.remove(f)
    print(f'删除旧: {f.name}')

# 保存
total = 0
for ch, qs in chapter_qs.items():
    if not qs: continue
    filename = f'习思-{ch}.json'
    data = {
        'name': f'习思-{ch}',
        'subject': '习思',
        'chapter': ch,
        'questions': qs
    }
    with open(DATA_DIR / filename, 'w', encoding='utf-8') as f:
        json.dump(data, f, ensure_ascii=False, indent=2)
    print(f'保存: {filename} ({len(qs)}题)')
    total += len(qs)

# 未分类
if unmatched:
    filename = '习思-综合.json'
    data = {'name':'习思-综合','subject':'习思','chapter':'综合','questions':unmatched}
    with open(DATA_DIR / filename, 'w', encoding='utf-8') as f:
        json.dump(data, f, ensure_ascii=False, indent=2)
    print(f'保存: {filename} ({len(unmatched)}题)')
    total += len(unmatched)

print(f'\n总计: {total} 题, {len(chapter_qs)} 个章节文件')
