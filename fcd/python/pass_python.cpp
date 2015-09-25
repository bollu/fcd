//
// pass_python.cpp
// Copyright (C) 2015 Félix Cloutier.
// All Rights Reserved.
//
// This file is part of fcd.
// 
// fcd is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
// 
// fcd is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
// 
// You should have received a copy of the GNU General Public License
// along with fcd.  If not, see <http://www.gnu.org/licenses/>.
//

#include "errors.h"
#include "pass_python.h"
#include <Python/Python.h>

using namespace llvm;
using namespace std;

#pragma mark - Refcounting magic
namespace
{
	struct PyDecRef
	{
		void operator()(PyObject* obj) const
		{
			Py_XDECREF(obj);
		}
	};
	
	typedef unique_ptr<PyObject, PyDecRef> AutoPyObject;
	
	struct WrapWithAutoPyObject
	{
		// operator|| is the last operator to have more precedence than operator=
		AutoPyObject operator||(PyObject* that) const
		{
			return AutoPyObject(that);
		}
	};
	
#define AUTO WrapWithAutoPyObject() ||
}

#pragma mark - Wrapper passes
namespace
{
	struct PythonWrapper
	{
		AutoPyObject module;
		AutoPyObject run;
		string name;
		
	public:
		PythonWrapper(AutoPyObject module, AutoPyObject run, const string& name)
		: module(move(module)), run(move(run)), name(name)
		{
		}
	};
	
	class PythonWrappedModule : public ModulePass, public PythonWrapper
	{
		static char ID;
		
	public:
		PythonWrappedModule(PythonWrapper wrapper)
		: ModulePass(ID), PythonWrapper(move(wrapper))
		{
		}
		
		virtual const char* getPassName() const override
		{
			return name.c_str();
		}
		
		virtual bool runOnModule(Module& m) override
		{
			return false;
		}
	};
}

#pragma mark - Implementation
PythonContext::PythonContext(const string& programPath)
{
	unique_ptr<char, decltype(free)&> mutableName(strdup(programPath.c_str()), free);
	Py_SetProgramName(mutableName.get());
	Py_Initialize();
}

ErrorOr<Pass*> PythonContext::createPass(const std::string &path)
{
	auto module = AUTO PyImport_ImportModule(path.c_str());
	auto runOnModule = AUTO PyObject_GetAttrString(module.get(), "runOnModule");
	auto runOnFunction = AUTO PyObject_GetAttrString(module.get(), "runOnFunction");
	
	unique_ptr<string> passName;
	if (auto passNameObj = AUTO PyObject_GetAttrString(module.get(), "passName"))
	if (auto asString = AUTO PyObject_Str(passNameObj.get()))
	{
		char* bufferPointer;
		Py_ssize_t stringLength;
		if (PyString_AsStringAndSize(asString.get(), &bufferPointer, &stringLength))
		{
			passName.reset(new string(bufferPointer, stringLength));
		}
	}
	
	if (runOnModule)
	{
		if (runOnFunction)
		{
			return make_error_code(FcdError::Python_PassTypeConfusion);
		}
		
		if (!PyCallable_Check(runOnModule.get()))
		{
			return make_error_code(FcdError::Python_InvalidPassFunction);
		}
		
		if (!passName)
		{
			passName.reset(new string("Python Module Pass"));
		}
		
		PythonWrapper wrapper(move(module), move(runOnModule), move(*passName));
	}
	else if (runOnFunction)
	{
		if (!PyCallable_Check(runOnModule.get()))
		{
			return make_error_code(FcdError::Python_InvalidPassFunction);
		}
		
		if (!passName)
		{
			passName.reset(new string("Python Function Pass"));
		}
		
		PythonWrapper wrapper(move(module), move(runOnModule), move(*passName));
	}
	else
	{
		return make_error_code(FcdError::Python_PassTypeConfusion);
	}
}

PythonContext::~PythonContext()
{
	Py_Finalize();
}

namespace llvm
{
	void initializePythonWrappedModulePass(PassRegistry& PR);
}
