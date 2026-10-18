if(WIN32)
  if(MSVC)
    # 源码含 UTF-8 中文注释；MSVC 默认系统代码页解析会把行尾注释的
    # 尾字节当成续行符，产生大面积语法错误。必须强制 /utf-8。
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /utf-8")
    set(CMAKE_SHARED_LINKER_FLAGS_RELEASE_INIT "${CMAKE_SHARED_LINKER_FLAGS_RELEASE_INIT} /LTCG")

    set(CMAKE_CXX_FLAGS_DEBUG_INIT "/D_DEBUG /MTd /Zi /Ob0 /Od /RTC1")
    set(CMAKE_CXX_FLAGS_MINSIZEREL_INIT     "/MT /O1 /Ob1 /D NDEBUG")
    # /GL + /LTCG：全程序优化（跨翻译单元内联/寄存器分配），配合
    # /O2 /Ob2。链接器侧由 CMAKE_SHARED_LINKER_FLAGS_RELEASE_INIT 提供 /LTCG。
    set(CMAKE_CXX_FLAGS_RELEASE_INIT        "/MT /O2 /Ob2 /GL /D NDEBUG")
    set(CMAKE_CXX_FLAGS_RELWITHDEBINFO_INIT "/MT /Zi /Od /Ob1 /D NDEBUG")
  elseif(MINGW)
    set(CMAKE_CXX_FLAGS_RELEASE_INIT "-D_MT -O3 -DNDEBUG")
  else()
    set(CMAKE_CXX_FLAGS_RELEASE_INIT "-D_MT -O3 -DNDEBUG")
    set(CMAKE_CXX_FLAGS ${CMAKE_CXX_FLAGS} "-Wno-error=deprecated-declarations")
  endif()
endif()
