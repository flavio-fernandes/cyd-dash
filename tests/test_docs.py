"""The README, CONTRIBUTING, docs/ and the agent files (AGENTS.md, CLAUDE.md, .claude/skills/):
every local link, image and anchor resolves, the skills have front matter, and the tone stays
friendly."""
from pathlib import Path
import re
import unittest
from urllib.parse import unquote, urlsplit

import yaml

ROOT = Path(__file__).resolve().parents[1]
SKILLS = sorted((ROOT / '.claude' / 'skills').glob('*/SKILL.md'))
AGENT_FILES = [p for p in (ROOT / 'AGENTS.md', ROOT / 'CLAUDE.md') if p.is_file()] + SKILLS
DOCUMENTS = [ROOT / 'README.md', ROOT / 'CONTRIBUTING.md'] + sorted((ROOT / 'docs').glob('*.md')) + AGENT_FILES
# Words that can make a reader feel slow. Allowed inside code.
TONE = re.compile(r'\b(simple|simply|just|obviously|trivially|trivial)\b', re.I)
FRONT_MATTER = re.compile(r'\A---\n(.*?)\n---\n', re.S)


def without_code(text):
    text = re.sub(r'^```.*?^```', '', text, flags=re.M | re.S)
    return re.sub(r'`[^`\n]*`', '', text)


def targets(text):
    text = re.sub(r'^```.*?^```', '', text, flags=re.M | re.S)
    found = re.findall(r'!?\[[^]\n]*\]\(<?([^\s)>]+)>?(?:\s+"[^"]*")?\)', text)
    found += re.findall(r'^\s*\[[^]]+\]:\s*<?([^\s>]+)>?', text, re.M)
    found += re.findall(r'<img\s[^>]*src="([^"]+)"', text)
    found += re.findall(r'<a\s[^>]*href="([^"]+)"', text)
    return found


def anchors(path):
    """GitHub's heading ids: lower case, punctuation dropped, spaces to hyphens."""
    seen, out = {}, set()
    for heading in re.findall(r'^#{1,6}\s+(.+?)\s*#*$', without_code(path.read_text()), re.M):
        slug = re.sub(r'[^\w\- ]', '', heading.strip().lower()).replace(' ', '-')
        n = seen.get(slug, 0); seen[slug] = n + 1
        out.add(slug if n == 0 else '%s-%d' % (slug, n))
    return out


def link_problems(text, folder, here):
    """(problems, checked) for the local links in `text`, read as if it lived in `folder`."""
    problems, checked = [], 0
    for link in targets(text):
        url = urlsplit(link)
        if url.scheme or url.netloc or link.startswith('mailto:'):
            continue
        target = (folder / unquote(url.path)).resolve() if url.path else here
        if not target.exists():
            problems.append('missing %s' % link)
        elif url.fragment and target.suffix == '.md' and url.fragment not in anchors(target):
            problems.append('no heading for %s' % link)
        checked += 1
    return problems, checked


def front_matter(text):
    match = FRONT_MATTER.match(text)
    return yaml.safe_load(match.group(1)) if match else None


def tone_problems(text):
    return [line.strip() for line in without_code(text).splitlines() if TONE.search(line)]


class DocLinkTests(unittest.TestCase):
    def test_documents_exist(self):
        for name in ['README.md', 'CONTRIBUTING.md', 'AGENTS.md', 'docs/README.md', 'docs/install.md',
                     'docs/configure.md', 'docs/using-your-cyd.md', 'docs/mqtt.md',
                     'docs/hardware.md', 'docs/ui-guidelines.md']:
            self.assertTrue((ROOT / name).is_file(), name)

    def test_links_images_and_anchors_resolve(self):
        checked = 0
        for document in DOCUMENTS:
            with self.subTest(document=str(document.relative_to(ROOT))):
                problems, n = link_problems(document.read_text(), document.parent, document)
                self.assertEqual(problems, [])
                checked += n
        self.assertGreater(checked, 40)

    def test_the_link_check_finds_a_broken_anchor(self):
        problems, _ = link_problems('[x](docs/mqtt.md#no-such-heading) [y](docs/nothing.md)',
                                    ROOT, ROOT / 'README.md')
        self.assertEqual(len(problems), 2)

    def test_readme_images_are_small(self):
        for link in targets((ROOT / 'README.md').read_text()):
            if link.startswith('docs/images/'):
                with self.subTest(image=link):
                    self.assertLess((ROOT / link).stat().st_size, 100_000)


class AgentFileTests(unittest.TestCase):
    def test_skills_have_front_matter(self):
        for skill in SKILLS:
            with self.subTest(skill=skill.parent.name):
                meta = front_matter(skill.read_text())
                self.assertIsInstance(meta, dict, 'no front matter')
                self.assertEqual(meta.get('name'), skill.parent.name)
                self.assertIn('Use when', meta.get('description', ''))
                self.assertIn('docs/', skill.read_text(), 'a skill points into the docs')

    def test_every_skill_folder_has_a_skill_file(self):
        folder = ROOT / '.claude' / 'skills'
        if folder.is_dir():
            for child in folder.iterdir():
                if child.is_dir():
                    self.assertTrue((child / 'SKILL.md').is_file(), child.name)

    @unittest.skipUnless((ROOT / 'CLAUDE.md').is_file(), 'CLAUDE.md is not in this checkout')
    def test_claude_md_imports_agents_md_and_lists_every_skill(self):
        text = (ROOT / 'CLAUDE.md').read_text()
        self.assertEqual(text.splitlines()[0].strip(), '@AGENTS.md')
        self.assertTrue(SKILLS, 'CLAUDE.md names skills, so .claude/skills/ must hold them')
        for skill in SKILLS:
            self.assertIn('.claude/skills/%s/SKILL.md' % skill.parent.name, text)

    def test_front_matter_reader(self):
        self.assertEqual(front_matter('---\nname: a\ndescription: Use when b.\n---\n# A\n')['name'], 'a')
        self.assertIsNone(front_matter('# No front matter\n'))


class QuickStartForHumansTests(unittest.TestCase):
    """The README's copy-and-paste prompt for a person's coding agent stays, and still points at
    what it names (tested end to end by a stranger's agent, up to a first boot on a CYD)."""

    def prompt(self):
        readme = (ROOT / 'README.md').read_text()
        section = readme.split('### Quick start for humans', 1)
        self.assertEqual(len(section), 2, 'README.md lost its "Quick start for humans" section')
        block = re.search(r'^```text\n(.*?)^```', section[1], re.M | re.S)
        self.assertIsNotNone(block, 'the section has no ```text prompt block')
        return block.group(1)

    def test_the_prompt_is_there_and_short(self):
        prompt = self.prompt()
        self.assertLessEqual(len(prompt.splitlines()), 30, 'keep the prompt to one screen')
        for words in ('AGENTS.md', 'docs/install.md', 'Hello World', 'pull request'):
            self.assertIn(words, prompt)

    def test_what_the_prompt_names_exists(self):
        prompt = self.prompt()
        section = re.search(r'"([^"]+)"', prompt.replace('\n   ', ' ')).group(1)
        install = (ROOT / 'docs' / 'install.md').read_text()
        self.assertIn('## ' + section, install, 'the prompt names a docs/install.md section that is gone')
        self.assertTrue((ROOT / 'AGENTS.md').is_file())


class ToneTests(unittest.TestCase):
    def test_no_words_that_make_readers_feel_slow(self):
        for document in DOCUMENTS:
            with self.subTest(document=str(document.relative_to(ROOT))):
                self.assertEqual(tone_problems(document.read_text()), [])

    def test_the_check_sees_prose_and_skips_code(self):
        self.assertTrue(TONE.search(without_code('It is just a step.')))
        self.assertFalse(TONE.search(without_code('Run `just build`.\n```\nsimple\n```\n')))


if __name__ == '__main__':
    unittest.main()
