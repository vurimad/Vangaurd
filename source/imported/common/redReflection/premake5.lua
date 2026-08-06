---
-- Copyright (C)2017 CD Projekt Red. All Rights Reserved.
---

-- globals redReflection

redReflection_location = '../../../../build/%{_ACTION}/projects/redReflection'

-- files redReflection

redReflection_files = {
	["api"] = {
		"include/redReflectionApi.h",
		"include/redReflectionPublic.h",
		"include/settings.h",
		"src/build.cpp",
		"src/build.h",
		"src/redReflectionDocumentation.cpp",
		"src/redReflectionInit.cpp",
		"src/redReflectionInternal.h",
	},

	-- reflection

	["reflection/access"] = {
		"src/rttiValueHolder.cpp",
		"src/rttiSingleValueHolder.cpp",
		"src/rttiAccessPath.cpp",
		"src/rttiPathParser.cpp",
		"src/rttiValueParser.cpp",
		"src/rttiValueBuilder.cpp",
		"include/rttiValueHolder.h",
		"include/rttiSingleValueHolder.h",
		"include/rttiAccessPath.h",
		"include/rttiPathParser.h",
		"include/rttiValueParser.h",
		"include/rttiValueBuilder.h",
	},
	["reflection/editorSupport"] = {
		"src/serializableEditorView.cpp",
		"include/serializableEditorView.h",
		"src/defaultSerializableEditorView.cpp",
		"include/defaultSerializableEditorView.h",
	},
	["reflection/types"] = {
		"include/rttiArrayTypes.h",
		"include/rttiArrayTypesImpl.h",
		"include/rttiComplexTypeCreator.h",
		"include/rttiFundamentalTypes.h",
		"include/rttiInternalTypeName.h",
		"include/rttiPointerTypes.h",
		"include/rttiPointerTypesImpl.h",
		"include/rttiSimpleType.h",
		"include/rttiType.h",
		"include/rttiTypeName.h",
		"src/rttiArrayTypes.cpp",
		"src/rttiArrayTypesImpl.cpp",
		"src/rttiComplexTypeCreator.cpp",
		"src/rttiFundamentalTypes.cpp",
		"src/rttiPointerTypes.cpp",
		"src/rttiPointerTypesImpl.cpp",
		"src/rttiSimpleType.cpp",
		"src/rttiType.cpp",
	},
	["reflection/types/enum"] = {
		"src/rttiEnum.cpp",
		"include/rttiEnum.h",
		"include/enumBuilder.h",
		"include/enumInternalBuilder.h",
	},
	["reflection/types/bitfield"] = {
		"src/rttiBitField.cpp",
		"include/rttiBitField.h",
		"include/bitFieldBuilder.h",
		"include/bitFieldInternalBuilder.h",
	},
	["reflection/types/variant"] = {
		"src/variant.cpp",
		"include/variant.h",
		"include/variant.hpp",
	},
	["reflection/system"] = {
		"include/rttiRegistration.h",
		"include/rttiSystem.h",
		"src/rttiRegistration.cpp",
		"src/rttiSystem.cpp",
		"src/rttiSystemImpl.cpp",
		"src/rttiSystemImpl.h",
	},
	["reflection"] = {
		"src/rttiUtils.cpp",
		"include/rttiCommon.h",
		"include/rttiTypeResolvingUtils.h",
		"include/rttiRegistrationMacros.h",
		"include/rttiUtils.h",
		"include/rttiMacrosUtils.h",
		"include/reflectionPool.h",
		"src/reflectionPool.cpp",
		"src/red_reflection.natvis",
	},
	["reflection/property"] = {
		"src/rttiPropertyBuilder.cpp",
		"src/rttiPropertyOverrideBuilder.cpp",
		"src/rttiProperty.cpp",
		"src/rttiCategoryMetadataBuilder.cpp",
		"include/rttiPropertyBuilder.h",
		"include/rttiPropertyOverrideBuilder.h",
		"include/rttiProperty.h",
		"include/rttiCategoryMetadataBuilder.h",
	},
	["reflection/function"] = {
		"include/rttiFunction.h",
		"include/rttiFunctionCalling.h",
		"include/rttiFunctionContext.h",
		"include/rttiFunctionNameDecoration.h",
		"include/rttiFunctionParamBuilder.h",
		"src/rttiFunction.cpp",
		"src/rttiFunctionCalling.cpp",
		"src/rttiFunctionContext.cpp",
		"src/rttiFunctionNameDecoration.cpp",
		"src/rttiFunctionParamBuilder.cpp",
	},
	["reflection/serializable"] = {
		"include/objectUtils.hpp",
		"include/postLoadContext.h",
		"include/serializable.h",
        "include/serializableDebug.h",
		"include/serializableId.h",
		"include/serializableValueNotifier.h",
		"src/serializable.cpp",
        "src/serializableDebug.cpp",
		"src/serializableCollector.cpp",
		"src/serializableId.cpp",
		"src/serializableValueNotifier.cpp",
		"include/serializableCollector.h",
	},
	["reflection/pointer"] = {
		"src/rttiPointer.cpp",
		"include/rttiPointer.h",
		"include/rttiPointer.inl",
	},
	["reflection/types/class"] = {
		"include/rttiAbstractClass.h",
		"include/rttiAbstractClass.inl",
		"include/rttiClass.h",
		"include/rttiClass.inl",
		"include/rttiNativeClass.h",
		"include/rttiNativeClass.inl",
		"src/rttiAbstractClass.cpp",
		"src/rttiClass.cpp",
		"src/rttiScriptedClass.cpp",
		"src/rttiScriptedClass.h",
	},
	["reflection/types/class/builder"] = {
		"include/rttiClassInternalBuilder.h",
		"include/rttiClassBuilder.h",
		"include/rttiFunctionMacros.h",
		"include/rttiClassDeclarationMacros.h",
		"include/rttiClassBuilderUtils.h",
		"src/rttiClassBuilderUtils.cpp",
	},
	["reflection/wrappers/containers"] = {
		"include/containersRTTI.h",
		"include/stringRTTI.h",
	},
	["reflection/map"] = {
		"src/serializableMap.cpp",
		"include/serializableMap.h",
		"include/serializableMap.hpp",
	},
	
	-- replication

	["reflection/replication"] = {
		"include/rttiReplicationBinding.h",
		"src/rttiReplicationBinding.cpp",
	},

	-- serialization

	["serialization/binary"] = {
		"include/pathResolver.h",
		"include/serializationBinaryStructureMapper.h",
		"include/serializationBinaryUtilities.h",
		"include/serializationBinaryRuntimeTables.h",
		"src/serializationBinaryLoader.cpp",
		"src/serializationBinaryLoader.h",
		"src/serializationBinaryRuntimeTables.cpp",
		"src/serializationBinarySaver.cpp",
		"src/serializationBinarySaver.h",
		"src/serializationBinaryStructureMapper.cpp",
		"src/serializationBinaryUtilities.cpp",
	},
	["serialization"] = {
		"include/serializationDecompressor.h",
		"include/serializationLoader.h",
		"include/serializationSaver.h",
		"include/serializationUtils.h",
		"src/serializationDecompressor.cpp",
		"src/serializationLoader.cpp",
		"src/serializationSaver.cpp",
		"src/serializationUtils.cpp",
	},
	["serialization/binary/format"] = {
		"include/serializationFileTables.h",
		"src/serializationFileTables.cpp",
		"src/serializationFileTablesBuilder.cpp",
		"src/serializationFileTablesBuilder.h",
	},
	["serialization/mapping"] = {
		"include/serializationMapping.h",
		"include/serializationNullMapper.h",
		"src/serializationMapping.cpp",
	},
	["serialization/async"] = {
		"include/serializationAsyncSource.h",
		"src/serializationAsyncSource.cpp",
	},
	["serialization/buffers/proxy"] = {
		"include/bufferAsyncProxy.h",
		"include/bufferHandle.h",
		"src/bufferAsyncProxy.cpp",
		"src/bufferHandle.cpp",
	},
	["serialization/utils"] = {
		"include/simpleBufferSerialization.h",
	},
	["serialization/containersWrappers"] = {
		"include/containersSerialization.h",
		"include/stringSerialization.h",
		"src/stringSerialization.cpp",
	},
	["serialization/buffers"] = {
		"include/dataBuffer.h",
		"include/deferredBufferMapper.h",
		"include/deferredDataBuffer.h",
		"include/sharedDataBuffer.h",
		"include/sharedDataBufferImpl.h",
		"src/dataBuffer.cpp",
		"src/deferredBufferMapper.cpp",
		"src/deferredDataBuffer.cpp",
		"src/sharedDataBuffer.cpp",
		"src/sharedDataBufferImpl.cpp",
	},

	-- scripting

	["scripting/runtime"] = {
		"src/scriptable.cpp",
		"include/scriptable.h",
		"include/scriptable.hpp",
		"include/scriptableContextLock.h",
		"src/scriptableContextLock.cpp",
		"include/scriptableThreadSafetyMonitorTypes.h",
		"include/scriptableThreadSafetyMonitor.h",
		"src/scriptableThreadSafetyMonitor.cpp",
		"src/scriptableThreadSafetyMonitorDump.cpp",
		"include/scriptFile.h",
		"src/scriptFile.cpp",
		"include/scriptVersion.h"
	},
	["scripting/runtime/breakpoints"] = {
		"include/scriptBreakpointResult.h",
		"include/scriptBreakpointRuntime.h",
		"src/scriptBreakpointResult.cpp",
		"src/scriptBreakpointRuntime.cpp",
	},
	["scripting/runtime/profiler"] = {
		"include/profilerMode.h",
		"include/scriptInstrumentationObjectRuntime.h",
		"src/scriptInstrumentationObjectRuntime.cpp"
	},
	["scripting/format"] = {
		"src/scriptDataObject.cpp",
		"src/scriptDataEnvironment.cpp",
		"src/scriptDataValidator.cpp",
		"src/scriptDataBinder.cpp",
		"src/scriptDataTypes.cpp",
		"src/scriptDataSaver.cpp",
		"src/scriptDataLoader.cpp",
		"include/scriptDataObject.h",
		"include/scriptDataSaver.h",
		"include/scriptDataLoader.h",
		"include/scriptDataEnvironment.h",
		"include/scriptDataValidator.h",
		"include/scriptDataBinder.h",
		"include/scriptDataTypes.h",
	},
	["scripting/virtualMachine"] = {
		"src/scriptArrayFunctions.cpp",
		"src/scriptArrayFunctions.h",
		"src/scriptOpcodes.cpp",
		"src/scriptOpcodesUtils.h",
		"src/scriptOpcodeTransformer.cpp",
		"include/scriptOpcodes.h",
		"include/scriptOpcodeTransformer.h",
		"include/scriptOpcodesList.h",
	},
	["scripting/format/fileBased"] = {
		"src/scriptDataFormat.cpp",
		"src/scriptDataSaverPrivate.cpp",
		"src/scriptDataLoaderPrivate.cpp",
		"include/scriptDataSaverPrivate.h",
		"include/scriptDataFormat.h",
		"include/scriptDataLoaderPrivate.h",
		"include/scriptUtils.h",
		"src/scriptUtils.cpp",
	},
	["scripting"] = {
		"src/scriptingSystem.cpp",
		"src/scriptingSystemImpl.cpp",
		"include/scriptingSystem.h",
		"include/scriptingSystemImpl.h",
	},
	["scripting/debug"] = {
		"src/scriptDebugger.h",
		"src/scriptDebuggerData.h",
		"src/scriptDebugger.cpp",
		"src/scriptableCyclesDetector.h",
		"src/scriptableCyclesDetector.cpp",
		"include/scriptLog.h",
		"src/scriptLog.cpp",
		"include/scriptBreakpointCondition.h",
		"src/scriptBreakpointCondition.cpp",
	},
	["scripting/debug/impl"] = {
		"src/scriptDebuggerImplBase.h",
		"src/scriptDebuggerImpl.h",
		"src/scriptDebuggerImpl.cpp",
	},
	["scripting/debug/impl/expressionParser/bison"] = {
		"src/scriptExpressionParser.bison",
	},
	["scripting/debug/impl/expressionParser/variables"] = {
		"src/scriptExpressionParserEnumVariable.h",
		"src/scriptExpressionParserEnumVariable.cpp",
		"src/scriptExpressionParserGeneratedVariable.h",
		"src/scriptExpressionParserGeneratedVariable.cpp",
	},
	["scripting/debug/impl/expressionParser"] = {
		"src/scriptExpressionParserToken.h",
		"src/scriptExpressionParser.h",
		"src/scriptExpressionParser.cpp",
		"src/scriptExpressionParserInternal.h",
		"src/scriptExpressionParserInternal.cpp",
		"src/scriptExpressionParserPath.h",
		"src/scriptExpressionParserPath.cpp",
		"src/scriptExpressionParserPathElement.h",
		"src/scriptExpressionParserPathElement.cpp",
	},
	["scripting/debug/impl/locals"] = {
		"src/scriptDebuggerLocalsBase.h",
		"src/scriptDebuggerLocalsObject.h",
		"src/scriptDebuggerLocalsProperty.h",
		"src/scriptDebuggerLocalsHandle.h",
		"src/scriptDebuggerLocalsWeakHandle.h",
		"src/scriptDebuggerLocalsSimple.h",
		"src/scriptDebuggerLocalsArray.h",
		"src/scriptDebuggerLocalsFrame.h",
		"src/scriptDebuggerLocalsVirtual.h",
		"src/scriptDebuggerLocalsBase.cpp",
		"src/scriptDebuggerLocalsObject.cpp",
		"src/scriptDebuggerLocalsProperty.cpp",
		"src/scriptDebuggerLocalsHandle.cpp",
		"src/scriptDebuggerLocalsWeakHandle.cpp",
		"src/scriptDebuggerLocalsSimple.cpp",
		"src/scriptDebuggerLocalsArray.cpp",
		"src/scriptDebuggerLocalsFrame.cpp",
		"src/scriptDebuggerLocalsVirtual.cpp",
	},
	["scripting/lowLevel"] = {
		"src/scriptCompiledCode.cpp",
		"src/scriptNativeFunctionMap.cpp",
		"src/scriptSnapshot.cpp",
		"src/scriptStackFrame.cpp",
		"include/scriptCompiledCode.h",
		"include/scriptNativeFunctionMap.h",
		"include/scriptSnapshot.h",
		"include/scriptStackFrame.h",
	},
	["scripting/natives"] = {
		"src/scriptCoreMath.cpp",
		"src/scriptCoreNatives.cpp",
		"src/scriptCoreOperators.cpp",
		"src/scriptCoreString.cpp",
		"src/scriptCoreCasts.cpp",
	},

	-- handles

	["handles"] = {
		"src/handle.cpp",
		"include/handle.h",
		"include/weakHandle.h",
		"include/handleSerialization.h",
		"include/handle.inl",
		"include/weakHandle.inl",
	},

	-- RESOURCE SYSTEM

	["resourceSystem"] = {
		"include/backendData.h",
		"include/gatheredResource.h",
		"include/resource.h",
		"include/resourceBank.h",
		"include/resourceCollector.h",
		"include/resourceCommon.h",
		"include/resourceListResource.h",
		"include/resourceLoader.h",
		"include/resourceLoaderScheduler.h",
		"include/resourceLoaderThrottler.h",
		"include/resourceLoaderTypes.h",
		"include/resourceLoadingFence.h",
		"include/resourceMetricsBank.h",
		"include/resourceMetrics.h",
		"include/resourceSnapshot.h",
		"include/resourceSwapList.h",
		"include/resourceToken.h",
		"include/serializationLoadingToken.h",
		"include/streamedResource.h",
		"include/streamedResourceCache.h",
		"include/streamedResourceDataExtractor.h",
		"include/streamedResourceListener.h",
		"include/streamedResourceManager.h",
		"include/version.h",
		"src/backendData.cpp",
		"src/gatheredResource.cpp",
		"src/resource.cpp",
		"src/resourceBank.cpp",
		"src/resourceCollector.cpp",
		"src/resourceListResource.cpp",
		"src/resourceLoader.cpp",
		"src/resourceLoaderScheduler.cpp",
		"src/resourceLoaderThrottler.cpp",
		"src/resourceLoaderThrottlerCrashData.cpp",
		"src/resourceLoaderThrottlerCrashData.h",
		"src/resourceLoadingFence.cpp",
		"src/resourceMetricsBank.cpp",
		"src/resourceMetrics.cpp",
		"src/resourceSnapshot.cpp",
		"src/resourceSwapList.cpp",
		"src/resourceToken.cpp",
		"src/streamedResource.cpp",
		"src/streamedResourceCache.cpp",
		"src/streamedResourceCacheUpdater.cpp",
		"src/streamedResourceDataExtractor.cpp",
		"src/streamedResourceManager.cpp",
	},
	["resourceSystem/resources"] = {
		"include/resourceAsyncReference.h",
		"include/resourceDepot.h",
		"include/resourceMonitor.h",
		"include/resourcePath.h",
		"include/resourcePathCache.h",
		"include/resourceReference.h",
		"include/resourceUtils.h",
		"src/resourceAsyncReference.cpp",
		"src/resourceDepot.cpp",
		"src/resourceMonitor.cpp",
		"src/resourcePath.cpp",
		"src/resourcePathCache.cpp",
		"src/resourceReference.cpp",
		"src/resourceUtils.cpp",
	},
	["resourceSystem/resources/prv"] = {
		"src/resourceClassLookup.cpp",
		"include/resourceClassLookup.h",
	},
	
	["Censorship"] = {
		"include/censorshipSystem.h",
		"src/censorshipSystem.cpp",
	},

	["DLC"] = {
		"include/dlcManifest.h",
		"include/dlcSystem.h",
		"src/dlcManifest.cpp",
		"src/dlcSystem.cpp",
	},	

	["fileWrappers"] = {
		"include/absoluteFilepath.h",
		"src/absoluteFilepath.cpp",
	},
	-- math wrappers

	["mathWrappers"] = {
		"src/math.cpp",
		"src/mathUtils.cpp",
		"src/mathTCBInterpolator.cpp",
		"include/mathForward.h",
		"include/mathUtils.h",
		"include/mathCommon.h", 
		"include/mathTCBInterpolator.h",
	},
	["mathWrappers/frustum"] = {
		"src/frustum.cpp",
		"include/frustum.h",
	},
	["mathWrappers/transform"] = {
		"include/mathTransform.h",
		"include/mathQSTransform.h",
		"src/mathTransform.cpp"
	},
	["mathWrappers/canonical"] = {
		"src/mathVector2.cpp",
		"src/mathVector3.cpp",
		"src/mathVector4.cpp",
		"src/mathMatrix.cpp",
		"src/mathSphere.cpp",
		"src/mathEulerAngles.cpp",
		"src/mathColor.cpp",
		"src/mathQuaternion.cpp",
		"include/mathBox.h",
		"include/mathPlane.h",
		"include/mathSegment.h",
		"include/mathVector2.h",
		"include/mathVector3.h",
		"include/mathVector4.h",
		"include/mathSphere.h",
		"include/mathMatrix.h",
		"include/mathRect.h",
		"include/mathPoint.h",
		"include/mathPoint3D.h",
		"include/mathEulerAngles.h",
		"include/mathQuaternion.h",
		"include/mathColor.h",
		"include/mathRectF.h",
	},
	["mathWrappers/curves"] = {
		"src/singleChannelCurve.cpp",
		"src/singleChannelCurve2.cpp",
		"src/multiChannelCurve.cpp",
		"src/multiChannelCurve2.cpp",
		"src/curveInterpolator.cpp",
		"src/curveInterpolator2.cpp",
		"src/curveEvaluator.cpp",
		"src/curveConverter.cpp",
		"include/singleChannelCurve.h",
		"include/singleChannelCurve2.h",
		"include/singleChannelCurve2.hpp",
		"include/multiChannelCurve.h",
		"include/multiChannelCurve2.h",
		"include/multiChannelCurve2.hpp",
		"include/curveInterpolator.h",
		"include/curveInterpolator2.h",
		"include/curveEvaluator.h",
		"include/curveConverter.h",
	},
	["mathWrappers/shapes"] = {
		"src/mathConvexHull.cpp",
		"src/mathConvexHullEx.cpp",
		"src/mathOrientedBox.cpp",
		"src/mathQuad.cpp",
		"src/mathTetrahedron.cpp",
		"src/mathFixedCapsule.cpp",
		"src/mathCylinder.cpp",
		"src/mathCutCone.cpp",
		"include/mathTetrahedron.h",
		"include/mathQuad.h",
		"include/mathCutCone.h",
		"include/mathCylinder.h",
		"include/mathOrientedBox.h",
		"include/mathFixedCapsule.h",
		"include/mathConvexHull.h",
		"include/mathConvexHullEx.h",
	},

	["mathWrappers/highPrecision"] = {
		"include/mathWorldPosition.h",
		"include/mathWorldTransform.h",
		"src/mathWorldPosition.cpp",
		"src/mathWorldTransform.cpp",
	},

	["mathWrappers/shapes/utils"] = {
		"src/shapeRenderer.cpp",
		"include/shapeRenderer.h",
	},
	["mathWrappers/shapes/convex"] = {
		"src/convexHullBuilder.cpp",
		"include/convexHullBuilder.h",
	},
	["mathWrappers/expression"] = {
		"src/expressionToolkit.cpp",
		"src/expressionToolkit_operators.cpp",
		"src/expressionToolkit_opRegistry.h",
		"include/expressionToolkit.h",
		"include/expressionToolkitHelper.h"
	},

	-- misc

	["misc/editorSupport"] = {
		"src/editorObjectId.cpp",
		"include/editorObjectId.h",
		"src/editorObjectIdPath.cpp",
		"include/editorObjectIdPath.h",
		"src/editableView.cpp",
		"include/editableView.h",
		"src/resourcePath.cpp",
		"include/resourcePath.h",
		"include/lastNodeSelection.h",
		"src/lastNodeSelection.cpp",
		"src/cloningUtils.cpp",
		"include/cloningUtils.h",
	},
	["misc/text"] = {
		"include/stringParser.h",
		"include/textReader.h",
		"include/textWriter.h",
		"src/stringParser.cpp",
	},
	["misc/text/impl"] = {
		"include/standardTextReader.h",
		"include/standardTextWriter.h",
		"src/standardTextReader.cpp",
		"src/standardTextWriter.cpp",
	},
	["misc/types"] = {
		"include/engineTime.h",
		"include/util.h",
		"include/types.h",
		"include/datetime.h",
		"include/typeList.h",
		"src/util.cpp",
		"src/engineTime.cpp",
		"src/engineTimeScripts.cpp",
		"src/datetime.cpp",
	},
	["misc/tools"] = {
		"src/redReflection.natstepfilter",
		"src/resourceSystem.natvis",
		"src/scriptDebugger.natvis",
		"src/serialization.natvis",
	},
	["misc/functionNameDecorator"] = {
		"src/functionNameDecorator.cpp",
		"include/functionNameDecorator.h",
	},
    ["misc/multiplayer"] = {
		"src/multiplayerSetup.cpp",
		"include/multiplayerSetup.h",
	},
	["package"] = {
		"include/packageVersion.h",
		"include/package.h",
		"src/package.cpp",
		"include/packageObjectLoader.h",
		"src/packageObjectLoader.cpp",
	},
	["package/layout"] =
	{
		"include/packageLayout.h",
		"include/packageLayoutLoader.h",
		"src/packageLayoutLoader.cpp",
		"include/packageLayoutSaver.h",
		"src/packageLayoutSaver.cpp",
	},
	["package/utils"] = 
	{
		"include/packageBuilder.h",
		"include/packageCompiler.h",
		"include/packageInspector.h",
		"include/packageIterator.h",
		"include/packageIterator.hpp",
		"include/packageRemapper.h",
		"include/packageUtils.h",
		"src/package.natvis",
		"src/packageBuilder.cpp",
		"src/packageCompiler.cpp",
		"src/packageInspector.cpp",
		"src/packageIterator.cpp",
		"src/packageRemapper.cpp",
		"src/packageUtils.cpp",
	},
	["package/serializer"] = 
	{
		"include/packageSerializer.h",
		"src/packageSerializer.cpp",
		"include/packageReader.h",
		"src/packageReader.cpp",
		"include/packageWriter.h",
		"src/packageWriter.cpp",
		"include/packageErrorReporter.h",
		"src/packageErrorReporter.cpp",
		"include/packageTypeSerializerDictionary.h",
		"src/packageTypeSerializerDictionary.cpp",
		"include/packageTypeSerializer.h",
		"src/packageTypeSerializer.cpp",
		"include/packageCustomTypeSerializer.h",
		"src/packageCustomTypeSerializer.cpp",
		"include/packageFundamentalSerializer.h",
		"src/packageFundamentalSerializer.cpp",
		"include/packageNameSerializer.h",
		"src/packageNameSerializer.cpp",
		"include/packageObjectSerializer.h",
		"src/packageObjectSerializer.cpp",
		"include/packageArraySerializer.h",
		"src/packageArraySerializer.cpp",
		"include/packageEnumSerializer.h",
		"src/packageEnumSerializer.cpp",
		"include/packageResourceSerializer.h",
		"src/packageResourceSerializer.cpp",
		"include/packageHandleSerializer.h",
		"src/packageHandleSerializer.cpp",
		"include/packageStringSerializer.h",
		"src/packageStringSerializer.cpp",
		"include/packageSimpleTypeSerializer.h",
		"src/packageSimpleTypeSerializer.cpp",
		"include/packageBitFieldSerializer.h",
		"src/packageBitFieldSerializer.cpp",
	},
	["package/stream"] = 
	{
		"include/packageStream.h",
		"src/packageStream.cpp",
		"include/packageWriteStream.h",
		"src/packageWriteStream.cpp",
		"include/packageReadStream.h",
		"src/packageReadStream.cpp",
		"include/packageReadWriteStream.h",
		"src/packageReadWriteStream.cpp",
	},
	["package/table"] =
	{
		"include/packageTable.h",
		"src/packageTable.cpp",
		"include/packageTableOfContent.h",
		"src/packageTableOfContent.cpp",
		"include/packageTableOfContentView.h",
		"src/packageTableOfContentView.cpp",
		"include/packageTableOfContentBuilder.h",
		"src/packageTableOfContentBuilder.cpp",
		"include/packageTableCompiler.h",
		"src/packageTableCompiler.cpp",
		"include/packageTableOfContentReader.h",
		"src/packageTableOfContentReader.cpp",
		"include/packageTableOfContentRemap.h",
		"src/packageTableOfContentRemap.cpp",
	},
	["event"] =
	{
		"include/eventTypes.h",
		"include/event.h",
		"src/event.cpp",
        "include/eventDebug.h",
		"src/eventDebug.cpp",
		"include/eventBroker.h",
		"src/eventBroker.cpp",
		"include/eventConnectorCollector.h",
		"src/eventConnectorCollector.cpp",
	},
	["delayedFunctionCalls"] =
	{
		"include/delayedFunctionCalls.h",
		"src/delayedFunctionCalls.cpp"
	},
	["resRefScriptWrapper"] =
	{
		"include/resourceReferenceScriptToken.h",
		"src/resourceReferenceScriptToken.cpp"
	}
}

-- configs redReflection

local function flatten_file_groups(groups)
	local result = {}
	for _, group in pairs(groups) do
		for _, file in ipairs(group) do table.insert(result, file) end
	end
	return result
end

project "redReflection"
    kind "StaticLib"
    language "C++"
	cppdialect "C++17"
	exceptionhandling "Off"

    location(redReflection_location)

    pchheader "build.h"
    pchsource "src/build.cpp"

    includedirs
	{
		"./",
		"include",
		"src",
		"../commProtocol/gen",
		"../../../temp/gen/redReflection",
		"../../../temp/gen/rtti/redReflection"
	}

	defines
	{
		"RED_MODULE_redReflection",
		"RED_EXPORT_redReflection"
	}

    links
	{
		"redMath",
		"redJobs2",
		"redConfig",
		"commChannel",
		"redLexer",
	}

	dependson
	{
		"redMath",
		"redJobs2",
		"redConfig",
		"commChannel",
		"redLexer",
	}

	filter "system:windows"
		prebuildcommands
		{
			'powershell.exe -NoProfile -ExecutionPolicy Bypass -File "' .. RED_VANGUARD_ROOT .. '/dev/tools/generate-red-reflection.ps1" -ProjectPath "%{prj.location}/%{prj.name}.vcxproj"'
		}
	filter {}

	filter "configurations:Release"
		-- MSVC 18.7 ICEs in optimized codegen for several legacy translation units.
		-- Keep /O2, but disable the SSA optimizer phase that is triggering the ICE.
		buildoptions { "/d2SSAOptimizer-" }
	filter {}

    vpaths(redReflection_files)

    files(flatten_file_groups(redReflection_files))

	filter "system:Linux"
		prebuildcommands {
			'{MKDIR} %{wks.location}/../gen/%{prj.name}',
			'BISON=%{wks.location}/../../external/bison/bin/bison.elf && \\',
			'export BISON_PKGDATADIR=%{wks.location}/../../external/bison/share/bison && \\',
			'$$BISON --defines="%{wks.location}/../gen/%{prj.name}/scriptExpressionParser_bison.cxx.h" -o "%{wks.location}/../gen/%{prj.name}/scriptExpressionParser_bison.cxx" "%{wks.location}/../../src/common/%{prj.name}/src/scriptExpressionParser.bison"'
		}
	filter {}
	
-- copy *.natstepfilter (https://msdn.microsoft.com/en-us/library/dn457346.aspx) to Visual Studio 2015 user visualizers directory
if _ACTION == 'vs2015' and premake.vstudio.uservisdir ~= nil then
	copy_absolute(script_dir() .. 'src/redReflection.natstepfilter', premake.vstudio.uservisdir .. 'redReflection.natstepfilter')
end
