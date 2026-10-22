# White Bird Engine

> 一个为节奏游戏开发而构建的免费/自由游戏引擎。
>
> 基于 [Apache License Version 2.0](https://github.com/OppositeNor/white-bird-engine/blob/main/LICENSE) 许可发布。

* [English](./README.md)

## 如何构建

克隆仓库：

```sh
git clone https://github.com/OppositeNor/white-bird-engine.git --recursive
cd white-bird-engine
```

或者：

```sh
git clone https://github.com/OppositeNor/white-bird-engine.git
cd white-bird-engine
git submodule update --init --recursive
```

安装构建依赖：

```sh
sudo pacman -S cmake clang ninja directx-shader-compiler
```

安装 [Miniforge](https://github.com/conda-forge/miniforge)，使用 Conda 管理构建环境。

### 配置构建环境

在仓库根目录创建并激活 Linux 构建环境：

```sh
conda env create -f environment.yml
conda activate white-bird-engine
```

更新已有环境：

```sh
conda env update -n white-bird-engine -f environment.yml
conda activate white-bird-engine
```

`environment.yml` 从 conda-forge 安装 Python 依赖，并指定 `libstdcxx-ng>=16`。
该运行库提供 `GLIBCXX_3.4.35`，满足当前系统工具链构建的原生模型编译器的要求。
更新环境即可解决 Conda 中旧版 `libstdc++.so.6` 的不兼容问题，无需使用 `LD_PRELOAD`。

构建脚本会自动构建本地的 `wbe-utils-model-compiler` 和 pyktx 绑定，无需单独通过 pip 安装。
运行 `build.py`、`./build-debug` 或 `validation.py` 时，请保持此环境处于激活状态。

C++ 运行库采用 GPLv3 **附加 GCC Runtime Library Exception 3.1** 许可。对于普通的 GCC/Clang 构建，
此例外允许链接运行库，同时保留本项目的 Apache-2.0 许可。若分发运行库本身，仍须遵守其许可，
包括适用的许可声明和源代码提供要求。详见 [GCC 运行库许可 FAQ](https://www.gnu.org/licenses/gcc-exception-3.1-faq.html)。

### 构建

```sh
python ./build.py
```

你也可以指定构建目标：

```sh
python ./build.py -t debug          # Debug 构建
python ./build.py -t release        # Release 构建
python ./build.py -t deploy         # 部署构建（默认）
python ./build.py -t debug-gcc      # 使用 GCC 的 Debug 构建
python ./build.py -t release-gcc    # 使用 GCC 的 Release 构建
python ./build.py -t deploy         # 使用 GCC 的部署构建
```

### 静态模型分类

`res/assets/models/models.json` 中的静态模型通过 `source_files` 将多个模型合并到共享几何缓冲区。
编译器输出一个 `<id>.mesh` 资源，包含 `opaque_instances`、`masked_instances` 和 `double_sided_opaque_instances` 三个分类。
glTF 中标记 `doubleSided: true` 的不透明材质属于第三类。
延迟渲染测试使用独立的不剔除背面的管线绘制此类几何，并翻转背面的光照法线。
详细配置见[模型编译器文档](dependencies/wbe-utils-model-compiler/README.md)。

## 贡献

### AI 的使用

本项目允许并鼓励使用 AI 助手参与开发。每次开启新的 AI 会话时，请先让 AI 通读
[AGENTS.md](./AGENTS.md)。该文件是专门面向 AI 代理编写的精简高密度参考资料，
涵盖了本项目的分层架构、构建流程、测试目录结构、内存模型以及命名规范等关键约定。

AI 完成编辑后，请**务必仔细审阅**每一处改动，确认你完全理解它所做的事情，然后再提交。
