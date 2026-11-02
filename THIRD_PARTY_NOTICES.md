# Attribution and third-party licensing

The original kwabai source is licensed under the [MIT License](LICENSE).
Dependencies and any third-party material retain their own copyright and license
terms. The MIT grant does not relicense KWin, Qt or other dependencies.

## yabai

[yabai](https://github.com/asmvik/yabai), by Åsmund Vikane, is the behavioral and
command-interface reference for kwabai. kwabai is an independent project and is
not endorsed by the yabai maintainers.

`scripts/compat/yabai` is a locally authored shell forwarder to the kwabai binary.
It does not contain or execute the upstream yabai program. Its filename is an
opt-in compatibility convenience, not a claim that kwabai is the upstream yabai
implementation or supports its entire interface.

The upstream notice is reproduced below for attribution and preservation of
notices for any upstream-derived material. Its inclusion does not assert that
all of kwabai is copied from yabai.

Source: [yabai LICENSE.txt](https://github.com/asmvik/yabai/blob/master/LICENSE.txt),
reviewed 2 November 2026.

```text
The MIT License (MIT)

Copyright (c) 2019 Åsmund Vikane

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:
The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.
THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

## Native KWin integration

The native effect and compatibility checker link to `KWin::kwin`. KWin's
[effect API](https://github.com/KDE/kwin/blob/master/src/effect/effect.h),
[window API](https://github.com/KDE/kwin/blob/master/src/window.h) and
[workspace API](https://github.com/KDE/kwin/blob/master/src/workspace.h) carry
`GPL-2.0-or-later` notices. This integration calls KWin functions and uses KWin
classes in the compositor process; it is not just a standalone command invocation.

MIT is GPL-compatible, so the MIT grant for kwabai's original source can be
preserved. For distributing the compiled KWin-linked plugin/checker, treat the
combined work as subject to the applicable GPL requirements, including license
notices and corresponding-source obligations. Do not describe those combined
binaries as free of GPL obligations merely because this repository has an MIT
license. Review the complete dependency set and distribution method before
publishing binary packages.

References:

- [GNU license compatibility list: Expat/MIT](https://www.gnu.org/licenses/license-list.html#Expat)
- [GNU FAQ: GPL programs and plug-ins](https://www.gnu.org/licenses/gpl-faq.en.html#GPLPlugins)

Qt, KDE Frameworks and Plasma Activities are external build/runtime dependencies;
this source checkout does not vendor their implementations. Their applicable
licenses and notices must also be respected when redistributing them.
