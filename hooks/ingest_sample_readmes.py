"""MkDocs hook: auto-ingest per-sample READMEs into the docs site.

Each `raytrace_tutorial/NN_name/README.md` is mirrored as a virtual page at
`samples/NN-name.md`. Repo-absolute references to `/docs/...` and
relative `../NN_name` cross-sample links are rewritten so the site renders
correctly while the on-disk READMEs remain canonical for GitHub browsing.
"""

from __future__ import annotations

import re
from pathlib import Path

from mkdocs.structure.files import File, Files

REPO_ROOT = Path(__file__).resolve().parent.parent
TUTORIAL_DIR = REPO_ROOT / "raytrace_tutorial"

# Folders under raytrace_tutorial/ that should NOT be ingested.
SKIP_FOLDERS = {"XX_template", "01_foundation_copy"}

# Maps repo-absolute /docs/* references (which work on GitHub) to their
# site-relative equivalents (rendered from samples/<name>.md).
DOCS_REWRITES = {
    "/docs/tutorial/index.md": "../tutorial/index.md",
    "/docs/getting-started/setup.md": "../getting-started/setup.md",
    "/docs/concepts/rendering.md": "../concepts/rendering.md",
    "/docs/concepts/acceleration-structures.md": "../concepts/acceleration-structures.md",
    "/docs/concepts/shader-binding-table.md": "../concepts/shader-binding-table.md",
}


def _folder_to_slug(folder_name: str) -> str:
    """`02_basic_nvvk` -> `02-basic-nvvk`."""
    return folder_name.replace("_", "-")


def _rewrite_markdown(md: str, folder_name: str, source_base: str) -> str:
    """Rewrite repo-relative URLs so they resolve on the rendered site.

    Order matters:
      1. Image references under /docs/images/ are mapped to the site's
         ../images/ folder.
      2. Specific repo-absolute /docs/*.md pages are mapped to their new
         site locations.
      3. Cross-sample relative links (`../NN_name`) are rewritten to
         site-internal `NN-name.md` links inside the samples/ section.
      4. Anything else still using `../` is assumed to point at a file
         outside the docs site and is converted to an absolute GitHub URL.
    """

    md = re.sub(r"\(/docs/images/", "(../images/", md)
    md = re.sub(r'src="/docs/images/', 'src="../images/', md)

    for old, new in DOCS_REWRITES.items():
        md = md.replace(f"]({old})", f"]({new})")
        md = md.replace(f"({old})", f"({new})")

    def _xref(match: re.Match[str]) -> str:
        target = match.group(1)
        return f"]({_folder_to_slug(target)}.md)"

    md = re.sub(
        r"\]\(\.\./([0-9]{2}_[a-z0-9_]+)(?:/README\.md|/)?\)",
        _xref,
        md,
    )

    base = source_base.rstrip("/")

    def _absolute(match: re.Match[str]) -> str:
        path = match.group(1)
        # Don't re-rewrite docs paths we already mapped to ../tutorial,
        # ../concepts, etc. - these resolve correctly relative to samples/.
        if path.startswith(("tutorial", "concepts", "getting-started", "images")):
            return match.group(0)
        return f"]({base}/raytrace_tutorial/{path})"

    md = re.sub(r"\]\(\.\./([^)\s]+)\)", _absolute, md)

    return md


def _build_page(folder: Path, source_base: str) -> str:
    """Return the rewritten markdown body with a 'View source' header."""
    raw = folder.joinpath("README.md").read_text(encoding="utf-8")
    body = _rewrite_markdown(raw, folder.name, source_base)

    github_url = f"{source_base.rstrip('/')}/raytrace_tutorial/{folder.name}"
    header = (
        f"!!! info \"Sample source\"\n"
        f"    Browse the full sample on GitHub: [`raytrace_tutorial/{folder.name}`]({github_url})\n\n"
    )
    return header + body


def on_files(files: Files, config) -> Files:
    """Inject one virtual `samples/NN-name.md` page per tutorial folder."""
    source_base = (config.get("extra") or {}).get(
        "source_base",
        "https://github.com/nvpro-samples/vk_raytracing_tutorial_KHR/blob/v2",
    )

    if not TUTORIAL_DIR.is_dir():
        return files

    for folder in sorted(TUTORIAL_DIR.iterdir()):
        if not folder.is_dir() or folder.name in SKIP_FOLDERS:
            continue
        if not folder.joinpath("README.md").is_file():
            continue
        # Only ingest folders that match the NN_name pattern (skips ad-hoc dirs).
        if not re.match(r"^\d{2}_", folder.name):
            continue

        slug = _folder_to_slug(folder.name)
        src_uri = f"samples/{slug}.md"
        if any(f.src_uri == src_uri for f in files):
            continue

        content = _build_page(folder, source_base)
        files.append(
            File.generated(config=config, src_uri=src_uri, content=content)
        )

    return files
