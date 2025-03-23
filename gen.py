def generate_sum_ir(n):
    # (n+1) 個の i32 パラメータ (%0 から %n) を受け取る関数のシグネチャを作成
    params = ", ".join([f"i32 noundef %{i}" for i in range(n + 1)])
    ir_lines = []
    ir_lines.append(f"define dso_local i32 @func({params}) local_unnamed_addr #0 {{")
    
    # SSA のルールを守るため、計算結果用のレジスタ名は %sum0, %sum1, ... とする
    # 初期値として %sum0 に %0 の値をセット
    ir_lines.append(f"  %sum0 = add nsw i32 0, %0")
    
    # パラメータ %1 から %n までを順次足し合わせる
    for i in range(1, n + 1):
        ir_lines.append(f"  %sum{i} = add nsw i32 %sum{i-1}, %{i}")
    
    # 最後のレジスタ %sum{n} が合計となる
    ir_lines.append(f"  ret i32 %sum{n}")
    ir_lines.append("}")
    return "\n".join(ir_lines)

# 使用例: 例えば n = 7 の場合（パラメータ %0 から %7 の合計）
if __name__ == "__main__":
    # get n as argument
    import sys
    n = int(sys.argv[1])
    llvm_ir = generate_sum_ir(n)
    print(llvm_ir)
