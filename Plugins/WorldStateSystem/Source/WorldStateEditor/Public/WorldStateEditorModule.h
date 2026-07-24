#pragma once

#include "Modules/ModuleManager.h"

/** Registers editor-only World State authoring and validation UI. */
class FWorldStateEditorModule final : public IModuleInterface
{
public:
	/** Installs the Participant Component details customization after PropertyEditor is available. */
	virtual void StartupModule() override;
	/** Removes the customization symmetrically when the editor module unloads. */
	virtual void ShutdownModule() override;
};
