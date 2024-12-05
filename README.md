# Tail Call Optimization - LLVM Pass

This repository contains an **LLVM Function Pass** designed to perform **tail call optimization (TCO)** on recursive functions. The goal of this pass is to transform recursive calls with an accumulator (e.g., factorial) into an iterative form to improve performance and reduce stack usage.

The pass is based on the [Writing an LLVM Pass](https://llvm.org/docs/WritingAnLLVMPass.html) guide and is implemented using the **legacy Pass Manager** infrastructure.

---

## 📜 Table of Contents

- [Introduction](#introduction)
- [Key Features](#key-features)
- [How It Works](#how-it-works)
- [Build Instructions](#build-instructions)
- [Usage](#usage)
- [Example Transformation](#example-transformation)
- [References](#references)

---

## 🧑‍💻 Introduction

Tail call optimization (TCO) is a compiler technique used to optimize recursive calls by reusing the current function's stack frame instead of allocating a new one for each call. This pass specifically targets recursive functions with an **accumulator** and converts them into an **iterative equivalent**. 

This is particularly useful for:
- Reducing stack overflow risks in deeply recursive functions.
- Improving runtime performance by removing function call overhead.

---

## ✨ Key Features

- Analyzes functions in LLVM IR and identifies tail-recursive patterns.
- Rewrites tail-recursive calls into loops using accumulator variables.
- Eliminates dead blocks after the transformation.
- Preserves the original function semantics.

---

## ⚙️ How It Works

This pass leverages LLVM's intermediate representation (IR) to:
1. **Identify Tail-Recursive Calls**: It detects recursive calls at the end of a function.
2. **Introduce an Accumulator**: Adds a parameter to the function to store intermediate results.
3. **Rewrite into a Loop**: Replaces the recursive calls with a loop construct.
4. **Remove Dead Blocks**: Cleans up unused blocks after the transformation.

---

## 🏗️ Build Instructions

To build the pass, follow these steps:

### Prerequisites
- **LLVM Development Environment**: Ensure LLVM is installed (e.g., version 10 or later).
- **CMake**: Required for building the pass.
- **Clang**: For compiling test cases and generating IR.

### Steps

1. Clone the repository:
   ```bash
   git clone https://github.com/your_username/tail-call-optimization
   cd tail-call-optimization
   ```

2. Create a build directory:
   ```bash
   mkdir build && cd build
   ```

3. Configure the project:
   ```bash
   cmake -DLLVM_DIR=/path/to/llvm ..
   ```

4. Build the pass:
   ```bash
   make
   ```

---

## 🚀 Usage

### Running the Pass

To use the pass, first compile your code to LLVM IR using `clang`:
```bash
clang -emit-llvm -S -o example.ll example.c
```

Run the pass using `opt`:
```bash
opt -load ./build/tail_call_optimization.so -tail-call-opt < example.ll > optimized.ll
```

---

## 🧪 Example Transformation

Consider the following C code for calculating the factorial of a number:

```c
int factorial(int n) {
    if (n <= 1)
        return 1;
    return n * factorial(n - 1);
}
```

### Steps to Transform:
1. Compile to LLVM IR:
   ```bash
   clang -emit-llvm -S -o factorial.ll factorial.c
   ```

2. Apply the optimization:
   ```bash
   opt -load ./build/tail_call_optimization.so -tail-call-opt < factorial.ll > factorial_optimized.ll
   ```

3. Inspect the optimized IR:
   ```bash
   cat factorial_optimized.ll
   ```

---

## 📖 References

- [Writing an LLVM Pass (Legacy PM)](https://llvm.org/docs/WritingAnLLVMPass.html)
- [LLVM IR Language Reference](https://llvm.org/docs/LangRef.html)
- [LLVM Developer Documentation](https://llvm.org/docs/)

---
