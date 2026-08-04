# Release Process

This document describes how to create a new release of ProtonForge with automatic .deb package building and GitHub release creation.

## 🚀 Automated Release Process

ProtonForge uses GitHub Actions to automatically build and release .deb packages when you create a new version tag.

### Step 1: Update Version

Update the version number in `CMakeLists.txt`:

```cmake
project(ProtonForge VERSION 1.0.3 LANGUAGES CXX)
```

**Important:** The GitHub Action will automatically use the tag version, but keeping CMakeLists.txt in sync is good practice.

### Step 2: Commit Changes

```bash
git add CMakeLists.txt
git commit -m "Bump version to 1.0.3"
git push origin master
```

### Step 3: Create and Push Tag

```bash
# Create a new tag
git tag -a v1.0.3 -m "Release version 1.0.3"

# Push the tag to GitHub
git push origin v1.0.3
```

### Step 4: Wait for Automation

Once the tag is pushed, GitHub Actions will automatically:

1. ✅ Build the release version of ProtonForge
2. ✅ Create the .deb package
3. ✅ Create a GitHub release
4. ✅ Upload the .deb package to the release
5. ✅ Generate release notes

**Check progress:**
- Go to: https://github.com/theinvisible/proton-forge/actions
- Watch the "Build and Release" workflow

### Step 5: Edit Release Notes (Optional)

After the automatic release is created:

1. Go to: https://github.com/theinvisible/proton-forge/releases
2. Find your new release
3. Click "Edit release"
4. Add/modify release notes if needed
5. Add screenshots or additional information

## 📋 What Gets Built

The automation creates:
- **Binary:** `ProtonForge` (Qt6 executable)
- **Package:** `protonforge_X.Y.Z_amd64.deb`
- **Release:** GitHub release with installation instructions

## 🔧 Manual Release (Fallback)

If you need to create a release manually:

```bash
# 1. Build release version
mkdir cmake-build-release && cd cmake-build-release
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . -j$(nproc)
cd ..

# 2. Create .deb package
bash build-deb.sh

# 3. Create GitHub release manually
# Go to: https://github.com/theinvisible/proton-forge/releases/new
# - Tag: v1.0.3
# - Title: ProtonForge v1.0.3
# - Upload: protonforge_1.0.3_amd64.deb
```

## 🐛 Manual Workflow Trigger

You can also trigger the release workflow manually:

1. Go to: https://github.com/theinvisible/proton-forge/actions
2. Select "Build and Release" workflow
3. Click "Run workflow"
4. Select branch and run

## 📊 CI/CD Workflows

ProtonForge has two GitHub Actions workflows:

### 1. CI Build (`ci.yml`)
- **Triggers:** Push to master/main/develop, Pull Requests
- **Purpose:** Verify builds work on every commit
- **Outputs:** Build artifacts (7 day retention)

### 2. Release Build (`release.yml`)
- **Triggers:** Version tags (v*.*.*)
- **Purpose:** Create official releases
- **Outputs:** GitHub release with .deb package

## 🏷️ Version Numbering

Use semantic versioning (SemVer):

- **v1.0.0** - Major release (breaking changes)
- **v1.1.0** - Minor release (new features, backwards compatible)
- **v1.1.1** - Patch release (bug fixes)

Examples:
```bash
# Major release
git tag -a v2.0.0 -m "ProtonForge 2.0 - Complete rewrite"

# Minor release
git tag -a v1.1.0 -m "Add Lutris support"

# Patch release
git tag -a v1.0.3 -m "Fix HDR detection bug"
```

## 🔐 Requirements

For the automation to work, ensure:

1. ✅ Repository has GitHub Actions enabled
2. ✅ `GITHUB_TOKEN` is available (automatic in GitHub Actions)
3. ✅ Repository has write permissions for releases

## 📝 Release Checklist

Before creating a new release:

- [ ] Unit tests pass: `ctest --test-dir cmake-build-debug --output-on-failure`
      (configure with `-DPROTONFORGE_BUILD_TESTS=ON` first)
- [ ] Integration tests pass: `tests/steam-lab/steamlab test`
      See `TESTS.md`. Three cases fail on purpose — they are the open findings in
      `TESTS.md §7`; check the list has not grown rather than that it is empty.
- [ ] CHANGELOG updated (if you have one)
- [ ] Version bumped in `CMakeLists.txt`
- [ ] README updated if needed
- [ ] Commits pushed to master
- [ ] Tag created and pushed
- [ ] GitHub Action completed successfully
- [ ] Release notes reviewed
- [ ] .deb package tested on Ubuntu 25.10 (or latest release)

## 🆘 Troubleshooting

**Build fails in GitHub Actions:**
- Check the Actions log for errors
- Ensure Qt6 dependencies are correct
- Test build locally first: `bash build-deb.sh`

**Release not created:**
- Verify tag format is correct: `v1.0.3` (not `1.0.3`)
- Check GitHub Actions permissions
- Look at workflow logs

**Package not uploaded:**
- Ensure `build-deb.sh` completed successfully
- Check file path in workflow matches actual output

## 📚 More Information

- **GitHub Actions Docs:** https://docs.github.com/en/actions
- **Semantic Versioning:** https://semver.org/
- **GitHub Releases:** https://docs.github.com/en/repositories/releasing-projects-on-github
