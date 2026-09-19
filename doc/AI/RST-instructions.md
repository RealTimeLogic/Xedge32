---
name: rst-instructions
description: Write or migrate Sphinx reStructuredText documentation that matches the Xedge32 documentation style, including sphinx_rtd_theme, Lua-oriented Sphinx extensions, heading hierarchy, cross-reference labels, admonitions, images, tables, command blocks, and API reference formatting.
---

# Xedge32 RST Documentation Skill

Use this skill when creating or updating a Sphinx documentation tree that should
look and read like the Xedge32 documentation in `doc/`.

## Core Workflow

1. Build a standard Sphinx tree with `conf.py`, `index.rst`, and topic pages
   under a `source/` directory.
2. Use `sphinx_rtd_theme` for HTML output.
3. Keep the writing practical, plain-English, and task-oriented. Explain what
   the API or workflow is for before listing signatures or options.
4. Add explicit cross-reference labels for pages and sections that other pages
   should link to.
5. Validate with Sphinx before delivery.

## Sphinx Configuration

Use this baseline in `conf.py`:

```python
project = 'Project Name'
copyright = '2026, RTL'
author = 'RTL'

master_doc = 'index'

extensions = [
    'sphinxcontrib.luadomain',
    'sphinx_lua',
    'sphinx.ext.autosectionlabel',
]

autosectionlabel_prefix_document = True

lua_source_path = ["./"]
lua_source_encoding = 'utf8'
lua_source_comment_prefix = '---'
lua_source_use_emmy_lua_syntax = True
lua_source_private_prefix = '_'

templates_path = ['_templates']
exclude_patterns = ['_build', 'Thumbs.db', '.DS_Store']

html_theme = 'sphinx_rtd_theme'
```

Install or provide the matching Sphinx dependencies:

```sh
pip install sphinx sphinx-rtd-theme sphinxcontrib-luadomain sphinx-lua
```

If the Lua extensions are not available and the task is only to validate
generic RST structure, run Sphinx with command-line config overrides as a
fallback rather than skipping validation.

## Directory Layout

Use this layout unless the target project already has a Sphinx structure:

```text
doc/
  conf.py
  index.rst
  Makefile
  make.bat
  source/
    GettingStarted.rst
    TopicName.rst
    img/
      screenshot.png
```

Keep topic pages in `doc/source/`. Reference them from `index.rst` with a
toctree path such as `source/GettingStarted` or `source/TopicName`.

## Index Page Pattern

Start `index.rst` with a short project introduction, an optional image, a
clear "what this manual covers" section, and then the toctree.

```rst
Project Introduction
====================

One or two short paragraphs explain the project and who this manual is for.

.. image:: https://example.com/image.svg
   :alt: Project name

What This Manual Covers
-----------------------

- the project-specific setup workflow,
- the runtime or API concepts unique to this project, and
- links to broader source-of-truth manuals when needed.

Project Manual
==============

.. toctree::
   :maxdepth: 3
   :caption: Contents:

   source/GettingStarted
   source/HardwareApi
   source/Tutorials
   source/license
```

Use full public URLs for external source-of-truth documentation when the
generated docs may be shared outside the local checkout.

## Heading Hierarchy

Use the same underline characters as the Xedge32 docs:

```rst
Page Title
==========

Major Section
-------------

Subsection
~~~~~~~~~~

Small Procedure Step
^^^^^^^^^^^^^^^^^^^^

Rare Deep Option
""""""""""""""""
```

Keep one top-level title per page. Use sentence-style titles that describe the
reader task or API topic.

## Labels and Cross-References

Add labels before pages or sections that other pages should reference:

```rst
.. _LuaShell32:

LuaShell32
==========
```

Use `:ref:` for internal links:

```rst
Continue with :ref:`LuaShell32`.
See :ref:`I2C API <I2C API>` for bus details.
```

Because `autosectionlabel_prefix_document = True` is enabled, prefer explicit
labels for important targets to avoid ambiguity.

Use `:download:` when linking a downloadable or media asset:

```rst
:download:`WebDAV how-to video <https://youtu.be/i5ubScGwUOc>`
```

Use `:guilabel:` for UI labels:

```rst
Click :guilabel:`Save`.
```

Use double backticks for inline code, file names, commands, literal values,
URLs shown as user-entered text, and Lua identifiers:

```rst
Open ``http://ip-address/rtl/apps/`` and create ``Lua-Examples``.
```

## External Links

Use standard RST inline links:

```rst
`Xedge documentation <https://realtimelogic.com/ba/doc/?url=Xedge.html>`_
```

Use anonymous links with `__` when repeating many one-off external targets in
the same section and you do not need to name or reuse the target.

## Code Blocks and Literal Blocks

Use `code-block` with a language whenever syntax highlighting is useful:

```rst
.. code-block:: lua

   local led <close> = esp32.gpio(18, "OUT")
   led:value(true)
```

Common languages in this doc style:

- `lua` for Xedge32 or BAS examples.
- `sh` for shell commands.
- `text` for output or protocol traces.

Use `:linenos:` only for long examples where line references help:

```rst
.. code-block:: lua
   :linenos:

   local function example()
      trace"hello"
   end
```

Use a literal block with `::` for short unhighlighted snippets:

```rst
The WebDAV URL is:

::

   https://ip-address/rtl/apps/
```

For command snippets styled by the theme or project CSS, use a container:

```rst
.. container:: cmd

   mako -l::FileServer
```

## Notes, Warnings, and Contents

Use admonitions sparingly and only when the callout changes reader behavior.

```rst
.. important::

   Make sure the command sequence matches your exact chip.

.. note::

   Some operations can block for up to the specified timeout.
```

For long setup pages, add a local page table of contents near the first deep
procedure section:

```rst
.. contents:: Page contents
   :depth: 2
   :local:
```

Prefer `important` for safety or compatibility constraints and `note` for
secondary operational details.

## Images and Figures

Use `image` for standalone screenshots or diagrams:

```rst
.. image:: img/esp-s3-usb.jpg
   :align: right
   :alt: ESP32-S3 USB ports
```

Use `figure` when the image needs a caption:

```rst
.. figure:: img/RMT-Symbol-Layout.png
   :align: center
   :alt: RMT-symbol layout

   Bit fields in an RMT symbol as defined by the hardware.
```

Always include meaningful `:alt:` text. Keep local images under
`doc/source/img/`. Public images may use full HTTPS URLs.

For a YouTube embed, use `raw:: html` only when the target output is HTML and a
plain link is not sufficient:

```rst
.. raw:: html

   <div style="position: relative;width: 100%;height: 0;padding-bottom: 56.25%;"><iframe src="https://www.youtube.com/embed/VIDEO_ID" frameborder="0" allowfullscreen style="position: absolute;top: 0;left: 0;width: 100%;height: 100%;"></iframe></div>
```

## Tables

Use `list-table` for small structured comparisons:

```rst
.. list-table::
   :header-rows: 1

   * - Binary File
     - Address
   * - ``merged-xedge.bin``
     - ``0x0``
```

Keep tables narrow enough for the Read the Docs theme. If a table becomes hard
to scan on mobile, rewrite it as bullets grouped by option.

## API Page Pattern

For hardware or module reference pages, use this order:

1. Plain-English purpose and when to use the API.
2. Typical workflow as a short numbered list.
3. Safety or hardware assumptions.
4. Object creation signature.
5. Parameters, return values, and configuration fields.
6. Object methods.
7. One practical example.
8. Practical guidance or troubleshooting.

Example skeleton:

```rst
I2C API
=======

The I2C API lets the project communicate with sensors and other peripherals.

The typical workflow is:

1. Create an I2C master object.
2. Probe the device address if needed.
3. Read from or write to the target device.
4. Close the bus object when you are done.

Creating an I2C Master
----------------------

Function signature:

.. code-block:: lua

   i2cm = esp32.i2cmaster(port, pinSDA, pinSCL, speed)

Parameters
~~~~~~~~~~

- ``port``: I2C controller number.
- ``pinSDA``: GPIO pin used for the SDA line.

``i2cm:close()``
~~~~~~~~~~~~~~~~

Closes the I2C master object and releases the associated resources.
```

## Writing Style

Use concise, direct prose:

- Start each page by explaining what the reader can do with the feature.
- Prefer "use this when..." and "start by..." guidance over abstract API
  descriptions.
- Keep examples realistic and complete enough to run.
- Mention cleanup and object lifetime for hardware resources.
- Put board, pin, voltage, address, and timeout caveats near the API they
  affect.
- Link to broader BAS, Xedge, ESP-IDF, or tutorial docs instead of duplicating
  generic background.
- Use "ESP32", "ESP32-S3", "Xedge32", "Xedge", "Barracuda App Server", "Lua",
  and "LuaShell32" consistently.

Avoid generic security, web, or platform boilerplate unless the target project
actually uses that mechanism.

## Formatting Details

- Wrap prose at a readable width, roughly 79 characters.
- Indent directive bodies by three spaces.
- Put a blank line before and after directives, lists, and code blocks.
- Use `#. ` only when automatic numbering is useful; otherwise use explicit
  `1.`, `2.`, `3.` style.
- Use bullet lists for options and troubleshooting.
- Use definition-list style for concept summaries:

```rst
**Xedge**
   The Lua application model and browser-based development environment.
```

- Keep local filenames and paths in double backticks.
- Use `:guilabel:` for UI controls, not bold text.

## Validation

From the repository root, run:

```sh
sphinx-build -b html doc doc/_build/html
```

Or from `doc/`:

```sh
make html
```

On Windows:

```bat
doc\make.bat html
```

If validation fails because optional Lua-domain extensions are missing and the
current task is only RST formatting, use Sphinx command-line overrides to
validate generic syntax:

```sh
sphinx-build -C -b html doc doc/_build/html
```

Do not consider the docs ready until Sphinx parses the changed files without
RST warnings introduced by the change.
