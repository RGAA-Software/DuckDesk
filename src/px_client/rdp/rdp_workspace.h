#pragma once

class QApplication;

namespace px::rdp {

// Entrypoint for the same px_client executable. Only the inherited stdin pipe
// carries its protected launch data; native capture/UDP/decoder modules are not constructed.
int RunClient(QApplication& application);

} // namespace px::rdp
