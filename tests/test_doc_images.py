"""Keep local Markdown image links usable in the published guide."""
from pathlib import Path
import re
import unittest
from urllib.parse import unquote, urlsplit

ROOT = Path(__file__).resolve().parents[1]


class ImageLinkTests(unittest.TestCase):
    def test_document_images_exist(self):
        for document in (ROOT / 'docs').glob('*.md'):
            text = document.read_text()
            references = dict(re.findall(r'^\s*\[([^]]+)\]:\s*<?([^\s>]+)>?', text, re.M))
            links = re.findall(r'!\[[^]]*\]\(<?([^\s)>]+)>?(?:\s+"[^"]*")?\)', text)
            for label, key in re.findall(r'!\[([^]]*)\]\[([^]]*)\]', text):
                self.assertIn(key or label, references, f'{document}: missing image reference')
                links.append(references[key or label])
            for link in links:
                url = urlsplit(link)
                if url.scheme or url.netloc:
                    continue
                with self.subTest(document=document.name, image=link):
                    self.assertTrue((document.parent / unquote(url.path)).is_file(),
                                    f'{document.name}: missing image {link}')


if __name__ == '__main__':
    unittest.main()
