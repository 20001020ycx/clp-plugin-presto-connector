/*
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

namespace facebook::presto {

/// Checks that the presto_server that loaded this plugin has as many virtual
/// functions as the plugin was compiled with, in each interface that the plugin
/// implements and whose vtable presto_server exports. presto_server doesn't
/// export the vtables of `protocol::ConnectorProtocol` and `VectorLoader`, so
/// those two interfaces aren't checked.
///
/// presto_server calls a plugin through these interfaces' vtables and doesn't
/// check them when it loads one, so a presto_server built from other Presto or
/// Velox sources can call the wrong function. Call this before registering
/// anything, since registration already calls into the plugin through these
/// interfaces.
///
/// @throw VeloxUserError if an interface differs.
void verifyHostCompatibility();

} // namespace facebook::presto
