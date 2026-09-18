#=============================================================================
# Copyright 2026 NVIDIA Corporation
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#=============================================================================
#
# Doxygen INPUT_FILTER. Turns references to C++ working-draft stable names of
# the form [exec.xyz] into hyperlinks to the corresponding section on
# https://eel.is/c++draft. The displayed text is left unchanged ([exec.xyz]),
# so the convention documented in CONTRIBUTING-docs.md still reads naturally in
# the source. Doxygen renders the emitted <a href> as a <ulink> in its XML,
# which Breathe turns into an external hyperlink in the Sphinx output.
#
# Doxygen invokes this as `perl eelis_link_filter.pl <input-file>`, so the file
# arrives on @ARGV and is streamed through <>. Everything that is not a stable
# name is passed through verbatim.

while (<>) {
    s{\[(exec(?:\.[a-z0-9_]+)+)\]}{<a href="https://eel.is/c++draft/$1">[$1]</a>}g;
    print;
}
