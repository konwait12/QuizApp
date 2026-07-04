"""从 浏览器下载 目录导入所有 docx 题库到 Project013"""
import os, json, re, sys
from pathlib import Path
from docx import Document

DOWNLOADS = Path(r'd:/桌面/Workspace/浏览器下载')
DATA_DIR = Path(__file__).parent.parent / 'data'

# 章节关键词映射 (用于自动归类)
CHAPTER_KEYWORDS = {
    '导论': ['导论', '马克思主义中国化', '马克思主义中国化时代化'],
    '第一章': ['毛泽东思想', '毛泽东'],
    '第二章': ['新民主主义革命', '新民主主义'],
    '第三章': ['社会主义改造', '改造理论', '社会主义制度'],
    '第四章': ['社会主义建设道路', '初步探索', '调动一切积极因素'],
    '第五章': ['邓小平理论', '邓小平', '社会主义本质'],
    '第六章': ['三个代表', '科学发展观', '中国特色社会主义理论体系'],
    '第七章': ['习近平新时代中国特色社会主义思想', '新时代', '习近平'],
    '第八章': ['中国特色社会主义总任务', '中国梦', '社会主义现代化', '中国式现代化'],
}

def parse_docx(filepath):
    """解析单个docx文件，返回题目列表"""
    doc = Document(filepath)
    questions = []
    current = None

    for p in doc.paragraphs:
        text = p.text.strip()
        if not text:
            continue

        style = p.style.name if p.style else ''

        if 'Heading 3' in style:
            # 新题目开始
            if current and current.get('q'):
                questions.append(current)
            current = {'q': '', 'options': [], 'ans': '', 'type': ''}

            # 提取题型
            if '单选题' in text:
                current['type'] = '单选'
            elif '多选题' in text:
                current['type'] = '多选'
            elif '判断题' in text:
                current['type'] = '判断'
            elif '简答' in text or '论述' in text:
                current['type'] = '简答'
            else:
                current['type'] = '单选'

        elif 'Heading 1' in style:
            pass  # 标题行，跳过
        elif '共' in text and '题' in text and '导出时间' in text:
            pass  # 统计行，跳过
        elif text.startswith('✓ 正确答案'):
            if current:
                ans = text.replace('✓ 正确答案:', '').replace('✓ 正确答案：', '').strip()
                current['ans'] = ans
        elif text.startswith('第') and '题' in text and 'Heading' not in style:
            pass  # 备选标题行
        elif current is not None:
            # 判断是选项还是题干
            if re.match(r'^[A-Z][.、\s]', text):
                current['options'].append(text.strip())
            elif not current['q']:
                current['q'] = text
            elif current['options']:
                # 可能是多行题干
                current['q'] += ' ' + text

    if current and current.get('q'):
        questions.append(current)

    return questions


def classify_chapter(questions):
    """根据题目内容判断章节"""
    all_text = ' '.join([q['q'] for q in questions])
    for chapter, keywords in CHAPTER_KEYWORDS.items():
        for kw in keywords:
            if kw in all_text:
                return chapter
    return None


def main():
    all_questions = []

    # 解析所有docx
    for f in sorted(DOWNLOADS.glob('quiz_export*.docx')):
        print(f'解析: {f.name}...', end=' ')
        try:
            qs = parse_docx(f)
            print(f'{len(qs)} 题')
            all_questions.extend(qs)
        except Exception as e:
            print(f'错误: {e}')

    print(f'\n总计: {len(all_questions)} 题')

    # 去重 (按题干)
    seen = set()
    unique = []
    for q in all_questions:
        key = q['q'][:50]
        if key not in seen:
            seen.add(key)
            q['id'] = f'q{len(unique)+1}'
            q['exp'] = ''
            unique.append(q)

    print(f'去重后: {len(unique)} 题')

    # 按章节分组
    chapters = {}
    unclassified = []

    for q in unique:
        # 尝试按关键词分类
        classified = False
        for chapter, keywords in CHAPTER_KEYWORDS.items():
            for kw in keywords:
                if kw in q['q']:
                    if chapter not in chapters:
                        chapters[chapter] = []
                    chapters[chapter].append(q)
                    classified = True
                    break
            if classified:
                break
        if not classified:
            unclassified.append(q)

    # 输出统计
    print('\n章节分布:')
    for ch, qs in sorted(chapters.items()):
        print(f'  {ch}: {len(qs)} 题')
    print(f'  未分类: {len(unclassified)} 题')

    # 尝试对未分类题目进行二次分类
    if unclassified:
        print('\n二次分类未分类题目...')
        for q in unclassified[:]:
            for chapter, keywords in CHAPTER_KEYWORDS.items():
                for kw in keywords:
                    if kw in q['q']:
                        if chapter not in chapters:
                            chapters[chapter] = []
                        chapters[chapter].append(q)
                        unclassified.remove(q)
                        break
                if q not in unclassified:
                    break

        print(f'  剩余未分类: {len(unclassified)} 题')

    # 保存
    DATA_DIR.mkdir(parents=True, exist_ok=True)
    saved_count = 0

    for chapter, qs in chapters.items():
        # 与已有数据合并
        existing_file = None
        for f in DATA_DIR.glob(f'*{chapter}*.json'):
            existing_file = f
            break

        if existing_file:
            # 加载已有数据
            with open(existing_file, 'r', encoding='utf-8') as f:
                existing = json.load(f)
            existing_qs = existing.get('questions', [])
            existing_stems = {q['q'][:50] for q in existing_qs}

            # 添加新题
            new_count = 0
            for q in qs:
                if q['q'][:50] not in existing_stems:
                    q['id'] = f'q{len(existing_qs) + new_count + 1}'
                    existing_qs.append(q)
                    existing_stems.add(q['q'][:50])
                    new_count += 1

            existing['questions'] = existing_qs
            with open(existing_file, 'w', encoding='utf-8') as f:
                json.dump(existing, f, ensure_ascii=False, indent=2)
            print(f'更新: {existing_file.name} (+{new_count}题)')
            saved_count += new_count
        else:
            # 新建文件
            filename = f'毛概-{chapter}.json'
            filepath = DATA_DIR / filename
            data = {
                'name': f'毛概-{chapter}',
                'subject': '毛概',
                'chapter': chapter,
                'questions': qs
            }
            with open(filepath, 'w', encoding='utf-8') as f:
                json.dump(data, f, ensure_ascii=False, indent=2)
            print(f'新建: {filename} ({len(qs)}题)')
            saved_count += len(qs)

    print(f'\n总计导入: {saved_count} 题')

    # 未分类的保存到单独文件
    if unclassified:
        filepath = DATA_DIR / '毛概-未分类.json'
        data = {
            'name': '毛概-未分类',
            'subject': '毛概',
            'chapter': '未分类',
            'questions': unclassified
        }
        with open(filepath, 'w', encoding='utf-8') as f:
            json.dump(data, f, ensure_ascii=False, indent=2)
        print(f'未分类题目: {filepath} ({len(unclassified)}题)')


if __name__ == '__main__':
    main()
