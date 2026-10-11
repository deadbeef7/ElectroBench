# ===========================================================================
# ElectroBench -- top-level Makefile
# ===========================================================================
# Desktop build produces build/ElectroBench. The Vita port lives under
# vitaport/ and produces ElectroBench-vita.vpk at the repository root.
# ===========================================================================

.PHONY: all clean vita vita-clean

all:
	@echo "Targets: vita  (PlayStation Vita VPK)"
	@echo "         clean (remove desktop build artifacts)"

vita:
	$(MAKE) -C vitaport

vita-clean:
	$(MAKE) -C vitaport clean

clean:
	rm -rf build
