#include "llvm/IR/DiagnosticInfo.h"

namespace llvm {

    class DLinker {
        public:
            DLinker(DiagnosticHandlerFunction DiagnosticHandler);
            ~DLinker();

            bool linkPsoRoot(Module* M);

        private:
            DataLayout DL;
            DiagnosticHandlerFunction DH;
    
    };
}
