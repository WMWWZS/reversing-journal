// 用 Ghidra 无界面模式反编译指定函数并打印伪 C 代码
//
// 用法：
//   analyzeHeadless.bat <proj_dir> <proj_name> -process <file> -noanalysis ^
//     -scriptPath <本目录> -postScript DecompileNamed.java take_damage
//
// 不传函数名则打印所有函数。

import ghidra.app.decompiler.DecompInterface;
import ghidra.app.decompiler.DecompileResults;
import ghidra.app.script.GhidraScript;
import ghidra.program.model.listing.Function;
import ghidra.program.model.listing.FunctionManager;

public class DecompileNamed extends GhidraScript {

    @Override
    public void run() throws Exception {
        String[] args = getScriptArgs();
        String target = (args != null && args.length > 0) ? args[0] : null;

        DecompInterface ifc = new DecompInterface();
        ifc.openProgram(currentProgram);

        FunctionManager fm = currentProgram.getFunctionManager();
        int shown = 0;

        for (Function f : fm.getFunctions(true)) {
            if (target != null && !f.getName().equals(target)) {
                continue;
            }
            if (f.isExternal()) {
                continue;
            }

            DecompileResults res = ifc.decompileFunction(f, 120, monitor);
            println("");
            println("================================================================");
            println("函数 : " + f.getName() + "   入口 : " + f.getEntryPoint());
            println("签名 : " + f.getPrototypeString(false, false));
            println("----------------------------------------------------------------");

            if (res.decompileCompleted()) {
                println(res.getDecompiledFunction().getC());
            } else {
                println("[反编译失败] " + res.getErrorMessage());
            }
            shown++;

            if (target != null) {
                break;
            }
        }

        println("");
        println("共输出 " + shown + " 个函数。");
        ifc.dispose();
    }
}
