#!/usr/bin/env python3
import re
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
MAKEFILE = ROOT / "Makefile"
CHANGELOG = ROOT / "CHANGELOG.md"

SEMVER = r"(\d+)\.(\d+)\.(\d+)"
RELEASE_TAG = re.compile(rf"^v{SEMVER}$")
VERSION_HEADING = re.compile(rf"^## {SEMVER}\s*$")
UNRELEASED_HEADING = re.compile(r"^## Unreleased\s*$")
SECTION_START = re.compile(r"^(?=## )", re.MULTILINE)
MAKEFILE_VERSION = re.compile(r"^(VERSION[ \t]*=[ \t]*)\S+[ \t]*$", re.MULTILINE)

CONVENTIONAL_SUBJECT = re.compile(
    r"^(?P<type>[a-z]+)(?:\((?P<scope>[^)]*)\))?(?P<bang>!)?:\s*(?P<description>.+)$"
)
RELEASE_COMMIT = re.compile(r"^chore\(release\): ")
TRAILER = re.compile(r"^[A-Za-z][\w-]*: .+$")
BREAKING_FOOTER = re.compile(r"^BREAKING[ -]CHANGE: ", re.MULTILINE)

SECTION_BY_TYPE = {
    "feat": "Added",
    "fix": "Fixed",
    "refactor": "Changed",
    "perf": "Changed",
    "revert": "Changed",
}
SECTION_ORDER = ["Breaking", "Added", "Fixed", "Changed", "Other"]


@dataclass
class Commit:
    sha: str
    type: str | None
    scope: str | None
    breaking: bool
    description: str
    body: str

    @property
    def section(self):
        if self.breaking:
            return "Breaking"
        return SECTION_BY_TYPE.get(self.type, "Other")


def git(*args):
    return subprocess.run(
        ["git", *args], cwd=ROOT, check=True, capture_output=True, text=True
    ).stdout


def version_of(match):
    return tuple(int(part) for part in match.groups())


def dotted(version):
    return ".".join(map(str, version))


def latest_release():
    matches = map(RELEASE_TAG.match, git("tag", "--list", "v*").split())
    return max(((version_of(match), match.string) for match in matches if match), default=None)


def without_trailers(body):
    paragraphs = body.strip().split("\n\n")
    if all(TRAILER.match(line) for line in paragraphs[-1].splitlines()):
        paragraphs.pop()
    return "\n\n".join(paragraphs)


def parse_commit(sha, message):
    subject, _, body = message.partition("\n")
    match = CONVENTIONAL_SUBJECT.match(subject)
    return Commit(
        sha=sha,
        type=match["type"] if match else None,
        scope=match["scope"] if match else None,
        breaking=bool(match and match["bang"]) or bool(BREAKING_FOOTER.search(body)),
        description=match["description"] if match else subject,
        body=without_trailers(body),
    )


def unreleased_commits(tag):
    log = git("log", "--no-merges", "--format=%H%x00%B%x1e", f"{tag}..HEAD")
    entries = [entry.strip("\n").split("\x00", 1) for entry in log.split("\x1e") if entry.strip("\n")]
    return [parse_commit(sha, message) for sha, message in entries if not RELEASE_COMMIT.match(message)]


def bumped(version, commits):
    major, minor, patch = version
    breaking = any(commit.breaking for commit in commits)
    features = any(commit.type == "feat" for commit in commits)
    fixes = any(commit.type == "fix" for commit in commits)
    if major == 0:
        return (0, minor + 1, 0) if breaking or features or fixes else (0, minor, patch + 1)
    if breaking:
        return (major + 1, 0, 0)
    if features:
        return (major, minor + 1, 0)
    return (major, minor, patch + 1)


def label_of(commit):
    if commit.section != "Other" or not commit.type:
        return commit.scope
    return f"{commit.type}({commit.scope})" if commit.scope else commit.type


def render_entry(commit):
    label = label_of(commit)
    prefix = f"**{label}:** " if label else ""
    description = commit.description[:1].upper() + commit.description[1:]
    entry = f"- {prefix}{description} ({commit.sha[:7]})"
    if commit.body:
        indented = "\n".join(f"  {line}" if line else "" for line in commit.body.splitlines())
        entry += f"\n\n{indented}"
    return entry


def render_notes(commits):
    sections = []
    for section in SECTION_ORDER:
        entries = [render_entry(commit) for commit in reversed(commits) if commit.section == section]
        if entries:
            sections.append(f"### {section}\n\n" + "\n\n".join(entries))
    return "\n\n".join(sections) + "\n"


def is_pending(section, released):
    heading = section.partition("\n")[0]
    if UNRELEASED_HEADING.match(heading):
        return True
    match = VERSION_HEADING.match(heading)
    return bool(match) and version_of(match) > released


def with_section(changelog, version, notes, released):
    intro, *sections = SECTION_START.split(changelog)
    while sections and is_pending(sections[0], released):
        sections.pop(0)
    intro = intro.rstrip("\n") + "\n\n" if intro.strip() else ""
    section = f"## {dotted(version)}\n\n{notes}" + ("\n" if sections else "")
    return intro + section + "".join(sections)


def with_version(makefile, version):
    updated, count = MAKEFILE_VERSION.subn(rf"\g<1>{dotted(version)}", makefile, count=1)
    if count != 1:
        sys.exit(f"bump: no VERSION line in {MAKEFILE}")
    return updated


def main():
    release = latest_release()
    if release is None:
        sys.exit("bump: no vX.Y.Z tag to count from; tag the last release first")
    released, tag = release
    commits = unreleased_commits(tag)
    if not commits:
        print(f"bump: nothing to release since {tag}")
        return
    version = bumped(released, commits)
    MAKEFILE.write_text(with_version(MAKEFILE.read_text(), version))
    CHANGELOG.write_text(with_section(CHANGELOG.read_text(), version, render_notes(commits), released))
    print(f"bump: {tag} -> v{dotted(version)}")


if __name__ == "__main__":
    main()
