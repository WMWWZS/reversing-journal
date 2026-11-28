// 从「导入函数」反查「谁在调用它」，并把调用者反编译出来。
//
// 这是逆向网络程序最快的一条路：
//   导入表里有 send / recv  →  谁引用它  →  那个函数就是发包/收包代码
//
// 用法：
//   analyzeHeadless.bat <proj_dir> <proj_name> -process <file> -noanalysis ^
//     -scriptPath <本目录> -postScript DecompileCallersOf.java send recv
//
// 参数：任意个外部函数名（默认 send recv）。
// 输出：每个名字 -> 被引用地址列表 -> 每个引用所在的函数 -> 反编译伪 C。

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.address.Address;
import ghidra.program.model.listing.Function;
import ghidra.program.model.symbol.Reference;
import ghidra.program.model.symbol.Symbol;
import ghidra.program.model.symbol.SymbolIterator;

import java.util.LinkedHashSet;
import java.util.Set;

public class DecompileCallersOf extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        if (args == null || args.length == 0) {
            args = new String[]{"send", "recv"};
        }

        DecompInterface ifc = new DecompInterface();
        ifc.openProgram(currentProgram);

        for (String name : args) {
            println("================================================================");
            println("外部函数: " + name);
            println("----------------------------------------------------------------");

            SymbolIterator it = currentProgram.getSymbolTable().getSymbols(name);
            int found = 0;
            while (it.hasNext()) {
                Symbol sym = it.next();
                Address target = sym.getAddress();
                println("  符号: " + sym.getName() + "  地址: " + target);

                Set<Function> funcs = new LinkedHashSet<>();
                for (Reference ref : getReferencesTo(target)) {
                    Address from = ref.getFromAddress();
                    println("    被引用 @ " + from + "  (" + ref.getReferenceType() + ")");
                    Function f = getFunctionContaining(from);
                    if (f != null) {
                        funcs.add(f);
                    }
                }

                for (Function f : funcs) {
                    println("");
                    println("  >>> 调用者函数: " + f.getName() + "  入口: " + f.getEntryPoint());
                    DecompileResults res = ifc.decompileFunction(f, 60, monitor);
                    if (res != null && res.decompileCompleted()) {
                        println(res.getDecompiledFunction().getC());
                    } else {
                        println("  (反编译失败)");
                    }
                }
                found++;
            }
            if (found == 0) {
                println("  (没找到这个符号；可能没有导入它，或者名字不同)");
            }
            println("");
        }
        ifc.dispose();
    }
}
