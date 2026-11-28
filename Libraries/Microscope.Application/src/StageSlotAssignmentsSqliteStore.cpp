#include "microscope/application/persistence/StageSlotAssignmentsSqliteStore.h"

#include <sqlite3.h>

#include <algorithm>
#include <limits>
#include <utility>

namespace microscope::application::persistence {
namespace {

using Error = StageSlotAssignmentsStoreError;
using ErrorCode = StageSlotAssignmentsStoreErrorCode;

constexpr int kSchemaVersion = 2;

Error makeError(ErrorCode code,sqlite3* database,std::string detail={}) {
    return Error{code,database==nullptr?0:sqlite3_errcode(database),
        detail.empty()&&database!=nullptr?sqlite3_errmsg(database):std::move(detail)};
}

bool execute(sqlite3* database,const char* sql) {
    char* message=nullptr;
    const int result=sqlite3_exec(database,sql,nullptr,nullptr,&message);
    sqlite3_free(message);
    return result==SQLITE_OK;
}

bool tableExists(sqlite3* database,const char* tableName) {
    sqlite3_stmt* statement=nullptr;
    if(sqlite3_prepare_v2(database,
        "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?1",
        -1,&statement,nullptr)!=SQLITE_OK) return false;
    const bool bound=sqlite3_bind_text(statement,1,tableName,-1,SQLITE_STATIC)==SQLITE_OK;
    const int result=bound?sqlite3_step(statement):SQLITE_ERROR;
    sqlite3_finalize(statement);
    return result==SQLITE_ROW;
}

class Statement final {
public:
    Statement(sqlite3* database,const char* sql) {
        prepared_=sqlite3_prepare_v2(database,sql,-1,&statement_,nullptr)==SQLITE_OK;
    }
    ~Statement() { if(statement_!=nullptr) sqlite3_finalize(statement_); }
    Statement(const Statement&)=delete;
    Statement& operator=(const Statement&)=delete;
    bool prepared() const noexcept { return prepared_; }
    sqlite3_stmt* get() const noexcept { return statement_; }
    int step() { return sqlite3_step(statement_); }
private:
    sqlite3_stmt* statement_=nullptr;
    bool prepared_=false;
};

class Transaction final {
public:
    explicit Transaction(sqlite3* database):database_(database) {
        active_=execute(database_,"BEGIN IMMEDIATE");
    }
    ~Transaction() {
        if(active_) {
            const bool rolledBack=execute(database_,"ROLLBACK");
            (void)rolledBack;
        }
    }
    bool active() const noexcept { return active_; }
    bool commit() {
        if(!active_||!execute(database_,"COMMIT")) return false;
        active_=false;
        return true;
    }
private:
    sqlite3* database_;
    bool active_=false;
};

bool bindText(sqlite3_stmt* statement,int index,const std::string& value) {
    return sqlite3_bind_text(statement,index,value.data(),
        static_cast<int>(value.size()),SQLITE_TRANSIENT)==SQLITE_OK;
}
bool bindRevision(sqlite3_stmt* statement,int index,std::uint64_t value) {
    if(value==0||value>static_cast<std::uint64_t>(
        std::numeric_limits<sqlite3_int64>::max())) return false;
    return sqlite3_bind_int64(statement,index,static_cast<sqlite3_int64>(value))==SQLITE_OK;
}
bool readPositiveRevision(sqlite3_stmt* statement,int column,std::uint64_t& value) {
    const auto raw=sqlite3_column_int64(statement,column);
    if(raw<=0) return false;
    value=static_cast<std::uint64_t>(raw);
    return true;
}

bool readCurrentAssignmentRevision(
    sqlite3* database,const core::MicroscopeId& microscopeId,
    std::uint64_t equipmentRevision,
    const core::EquipmentComponentId& stageComponentId,
    const core::StageConfigurationId& configurationId,
    std::optional<std::uint64_t>& revision) {
    Statement statement(database,
        "SELECT revision FROM stage_slot_assignment_current "
        "WHERE microscope_id=?1 AND equipment_revision=?2 "
        "AND stage_component_id=?3 AND configuration_id=?4");
    if(!statement.prepared()||
       !bindText(statement.get(),1,microscopeId.value())||
       !bindRevision(statement.get(),2,equipmentRevision)||
       !bindText(statement.get(),3,stageComponentId.value())||
       !bindText(statement.get(),4,configurationId.value())) return false;
    const int result=statement.step();
    if(result==SQLITE_DONE) {
        revision.reset();
        return true;
    }
    if(result!=SQLITE_ROW) return false;
    std::uint64_t found=0;
    if(!readPositiveRevision(statement.get(),0,found)||statement.step()!=SQLITE_DONE)
        return false;
    revision=found;
    return true;
}

bool readCurrentSlideRevision(
    sqlite3* database,const core::SlideId& slideId,
    std::optional<std::uint64_t>& revision) {
    Statement statement(database,
        "SELECT revision FROM slide_current WHERE slide_id=?1");
    if(!statement.prepared()||!bindText(statement.get(),1,slideId.value())) return false;
    const int result=statement.step();
    if(result==SQLITE_DONE) {
        revision.reset();
        return true;
    }
    if(result!=SQLITE_ROW) return false;
    std::uint64_t found=0;
    if(!readPositiveRevision(statement.get(),0,found)||statement.step()!=SQLITE_DONE)
        return false;
    revision=found;
    return true;
}

const core::StageConfigurationSpecification* findConfiguration(
    const core::StageSpecification& stage,const core::StageConfigurationId& id) {
    const auto it=std::find_if(stage.configurations().begin(),stage.configurations().end(),
        [&id](const core::StageConfigurationSpecification& item){return item.id()==id;});
    return it==stage.configurations().end()?nullptr:&*it;
}

bool bindReal(sqlite3_stmt* statement,int index,double value) {
    return std::isfinite(value)&&sqlite3_bind_double(statement,index,value)==SQLITE_OK;
}

const char* cornerName(navigation::StageSlotRegistrationCorner corner) {
    switch(corner) {
    case navigation::StageSlotRegistrationCorner::TopLeft:return "top-left";
    case navigation::StageSlotRegistrationCorner::TopRight:return "top-right";
    case navigation::StageSlotRegistrationCorner::BottomRight:return "bottom-right";
    case navigation::StageSlotRegistrationCorner::BottomLeft:return "bottom-left";
    }
    return nullptr;
}
std::optional<navigation::StageSlotRegistrationCorner> parseCornerName(
    const std::string& value) {
    if(value=="top-left") return navigation::StageSlotRegistrationCorner::TopLeft;
    if(value=="top-right") return navigation::StageSlotRegistrationCorner::TopRight;
    if(value=="bottom-right") return navigation::StageSlotRegistrationCorner::BottomRight;
    if(value=="bottom-left") return navigation::StageSlotRegistrationCorner::BottomLeft;
    return std::nullopt;
}

const char* observationSourceName(core::ObservationSource source) {
    switch(source) {
    case core::ObservationSource::HardwareReadback:return "hardware-readback";
    case core::ObservationSource::DeviceEvent:return "device-event";
    case core::ObservationSource::SdkStatus:return "sdk-status";
    case core::ObservationSource::OperatorConfirmation:return "operator-confirmation";
    case core::ObservationSource::CalibratedEstimate:return "calibrated-estimate";
    case core::ObservationSource::AdapterReport:return "adapter-report";
    case core::ObservationSource::Unknown:return nullptr;
    }
    return nullptr;
}
std::optional<core::ObservationSource> parseObservationSourceName(
    const std::string& value) {
    if(value=="hardware-readback") return core::ObservationSource::HardwareReadback;
    if(value=="device-event") return core::ObservationSource::DeviceEvent;
    if(value=="sdk-status") return core::ObservationSource::SdkStatus;
    if(value=="operator-confirmation") return core::ObservationSource::OperatorConfirmation;
    if(value=="calibrated-estimate") return core::ObservationSource::CalibratedEstimate;
    if(value=="adapter-report") return core::ObservationSource::AdapterReport;
    return std::nullopt;
}

std::optional<navigation::StageSlotRegistration> readStageSlotRegistration(
    sqlite3* database,const core::MicroscopeId& microscopeId,
    std::uint64_t equipmentRevision,const core::EquipmentComponentId& stageComponentId,
    const core::StageConfigurationId& configurationId,std::uint64_t assignmentRevision,
    const core::StageSlotId& slotId,const core::SlideId& slideId,
    std::uint64_t slideRevision,bool& valid) {
    Statement metadata(database,
        "SELECT calibration_revision_id,slide_x_extent_micrometers,"
        "slide_y_extent_micrometers,accepted_maximum_fit_residual_nm "
        "FROM stage_slot_assignment_registration "
        "WHERE microscope_id=?1 AND equipment_revision=?2 AND stage_component_id=?3 "
        "AND configuration_id=?4 AND revision=?5 AND stage_slot_id=?6");
    if(!metadata.prepared()||
       !bindText(metadata.get(),1,microscopeId.value())||
       !bindRevision(metadata.get(),2,equipmentRevision)||
       !bindText(metadata.get(),3,stageComponentId.value())||
       !bindText(metadata.get(),4,configurationId.value())||
       !bindRevision(metadata.get(),5,assignmentRevision)||
       !bindText(metadata.get(),6,slotId.value())) {
        valid=false;
        return std::nullopt;
    }
    const int metadataResult=metadata.step();
    if(metadataResult==SQLITE_DONE) return std::nullopt;
    if(metadataResult!=SQLITE_ROW) {
        valid=false;
        return std::nullopt;
    }

    const auto* calibrationText=sqlite3_column_text(metadata.get(),0);
    const int calibrationBytes=sqlite3_column_bytes(metadata.get(),0);
    const double slideXMicrometres=sqlite3_column_double(metadata.get(),1);
    const double slideYMicrometres=sqlite3_column_double(metadata.get(),2);
    const double acceptedResidual=sqlite3_column_double(metadata.get(),3);
    if(calibrationText==nullptr||calibrationBytes<=0||
       !std::isfinite(slideXMicrometres)||!std::isfinite(slideYMicrometres)||
       !std::isfinite(acceptedResidual)||acceptedResidual<0.0||
       slideXMicrometres<=0.0||slideYMicrometres<=0.0||
       metadata.step()!=SQLITE_DONE) {
        valid=false;
        return std::nullopt;
    }
    auto calibrationRevision=core::CalibrationRevisionId::create(std::string(
        reinterpret_cast<const char*>(calibrationText),
        static_cast<std::size_t>(calibrationBytes)));
    auto geometry=core::SlideGeometry::create(slideXMicrometres,slideYMicrometres);
    if(!calibrationRevision||!geometry) {
        valid=false;
        return std::nullopt;
    }

    Statement corners(database,
        "SELECT corner,stage_x_nm,stage_y_nm,observation_source,uncertainty_nm "
        "FROM stage_slot_assignment_registration_corner "
        "WHERE microscope_id=?1 AND equipment_revision=?2 AND stage_component_id=?3 "
        "AND configuration_id=?4 AND revision=?5 AND stage_slot_id=?6");
    if(!corners.prepared()||
       !bindText(corners.get(),1,microscopeId.value())||
       !bindRevision(corners.get(),2,equipmentRevision)||
       !bindText(corners.get(),3,stageComponentId.value())||
       !bindText(corners.get(),4,configurationId.value())||
       !bindRevision(corners.get(),5,assignmentRevision)||
       !bindText(corners.get(),6,slotId.value())) {
        valid=false;
        return std::nullopt;
    }
    std::array<std::optional<navigation::StageSlotCornerMeasurement>,4> ordered;
    int result=SQLITE_ROW;
    while((result=corners.step())==SQLITE_ROW) {
        const auto* cornerText=sqlite3_column_text(corners.get(),0);
        const auto* sourceText=sqlite3_column_text(corners.get(),3);
        const int cornerBytes=sqlite3_column_bytes(corners.get(),0);
        const int sourceBytes=sqlite3_column_bytes(corners.get(),3);
        const double x=sqlite3_column_double(corners.get(),1);
        const double y=sqlite3_column_double(corners.get(),2);
        const double uncertainty=sqlite3_column_double(corners.get(),4);
        if(cornerText==nullptr||sourceText==nullptr||cornerBytes<=0||sourceBytes<=0||
           !std::isfinite(x)||!std::isfinite(y)||!std::isfinite(uncertainty)||uncertainty<0.0) {
            valid=false;
            return std::nullopt;
        }
        const std::string cornerValue(reinterpret_cast<const char*>(cornerText),
            static_cast<std::size_t>(cornerBytes));
        const std::string sourceValue(reinterpret_cast<const char*>(sourceText),
            static_cast<std::size_t>(sourceBytes));
        const auto corner=parseCornerName(cornerValue);
        const auto source=parseObservationSourceName(sourceValue);
        if(!corner||!source) {
            valid=false;
            return std::nullopt;
        }
        const auto index=static_cast<std::size_t>(*corner);
        if(index>=ordered.size()||ordered[index]) {
            valid=false;
            return std::nullopt;
        }
        auto measurement=navigation::StageSlotCornerMeasurement::create(
            *corner,{x,y},*source,uncertainty);
        if(!measurement) {
            valid=false;
            return std::nullopt;
        }
        ordered[index]=std::move(measurement).value();
    }
    if(result!=SQLITE_DONE) {
        valid=false;
        return std::nullopt;
    }
    for(const auto& measurement:ordered) {
        if(!measurement) {
            valid=false;
            return std::nullopt;
        }
    }
    std::array<navigation::StageSlotCornerMeasurement,4> measurements{
        *ordered[0],*ordered[1],*ordered[2],*ordered[3]};
    auto registration=navigation::StageSlotRegistration::create(
        slotId,slideId,slideRevision,std::move(calibrationRevision).value(),
        std::move(geometry).value(),std::move(measurements),acceptedResidual);
    if(!registration) {
        valid=false;
        return std::nullopt;
    }
    return std::move(registration).value();
}

} // namespace

StageSlotAssignmentsSqliteStore::StageSlotAssignmentsSqliteStore(sqlite3* database) noexcept
    :database_(database) {}

core::Result<std::unique_ptr<StageSlotAssignmentsSqliteStore>,StageSlotAssignmentsStoreError>
StageSlotAssignmentsSqliteStore::open(const std::string& databasePath) {
    if(databasePath.empty())
        return core::Result<std::unique_ptr<StageSlotAssignmentsSqliteStore>,
            StageSlotAssignmentsStoreError>::failure(
                Error{ErrorCode::EmptyDatabasePath,0,"Database path is empty."});

    sqlite3* database=nullptr;
    if(sqlite3_open_v2(databasePath.c_str(),&database,
        SQLITE_OPEN_READWRITE|SQLITE_OPEN_CREATE|SQLITE_OPEN_FULLMUTEX,nullptr)!=SQLITE_OK) {
        const auto error=makeError(ErrorCode::DatabaseOpenFailed,database);
        if(database!=nullptr) sqlite3_close_v2(database);
        return core::Result<std::unique_ptr<StageSlotAssignmentsSqliteStore>,
            StageSlotAssignmentsStoreError>::failure(error);
    }
    sqlite3_busy_timeout(database,5000);
    if(!execute(database,"PRAGMA foreign_keys=ON")) {
        const auto error=makeError(ErrorCode::DatabaseOpenFailed,database);
        sqlite3_close_v2(database);
        return core::Result<std::unique_ptr<StageSlotAssignmentsSqliteStore>,
            StageSlotAssignmentsStoreError>::failure(error);
    }

    auto store=std::unique_ptr<StageSlotAssignmentsSqliteStore>(
        new StageSlotAssignmentsSqliteStore(database));
    const auto initialized=store->initializeSchema();
    if(!initialized)
        return core::Result<std::unique_ptr<StageSlotAssignmentsSqliteStore>,
            StageSlotAssignmentsStoreError>::failure(initialized.error());
    return core::Result<std::unique_ptr<StageSlotAssignmentsSqliteStore>,
        StageSlotAssignmentsStoreError>::success(std::move(store));
}

StageSlotAssignmentsSqliteStore::~StageSlotAssignmentsSqliteStore() {
    if(database_!=nullptr) sqlite3_close_v2(database_);
}

core::Status<StageSlotAssignmentsStoreError>
StageSlotAssignmentsSqliteStore::initializeSchema() {
    if(!tableExists(database_,"slide_revision")||
       !tableExists(database_,"slide_current"))
        return core::Status<StageSlotAssignmentsStoreError>::failure(
            makeError(ErrorCode::SampleSlideStoreSchemaRequired,database_,
                "SampleSlideSqliteStore must initialize slide_revision and slide_current first."));

    if(!execute(database_,
        "CREATE TABLE IF NOT EXISTS stage_slot_assignments_store_metadata ("
        "singleton INTEGER PRIMARY KEY CHECK(singleton=1),"
        "schema_version INTEGER NOT NULL)"))
        return core::Status<StageSlotAssignmentsStoreError>::failure(
            makeError(ErrorCode::SchemaMigrationFailed,database_));

    Statement insertMetadata(database_,
        "INSERT OR IGNORE INTO stage_slot_assignments_store_metadata "
        "(singleton,schema_version) VALUES(1,1)");
    if(!insertMetadata.prepared()||insertMetadata.step()!=SQLITE_DONE)
        return core::Status<StageSlotAssignmentsStoreError>::failure(
            makeError(ErrorCode::SchemaMigrationFailed,database_));

    Statement readMetadata(database_,
        "SELECT schema_version FROM stage_slot_assignments_store_metadata WHERE singleton=1");
    if(!readMetadata.prepared()||readMetadata.step()!=SQLITE_ROW)
        return core::Status<StageSlotAssignmentsStoreError>::failure(
            makeError(ErrorCode::SchemaMigrationFailed,database_));
    const auto storedSchemaVersion=sqlite3_column_int64(readMetadata.get(),0);
    if((storedSchemaVersion!=1&&storedSchemaVersion!=kSchemaVersion)||
       readMetadata.step()!=SQLITE_DONE)
        return core::Status<StageSlotAssignmentsStoreError>::failure(
            makeError(ErrorCode::UnsupportedSchemaVersion,database_));

    const char* schema=R"SQL(
        CREATE TABLE IF NOT EXISTS stage_slot_assignment_revision (
            microscope_id TEXT NOT NULL,
            equipment_revision INTEGER NOT NULL CHECK(equipment_revision>0),
            stage_component_id TEXT NOT NULL,
            configuration_id TEXT NOT NULL,
            revision INTEGER NOT NULL CHECK(revision>0),
            PRIMARY KEY(
                microscope_id,equipment_revision,stage_component_id,
                configuration_id,revision
            )
        );

        CREATE TABLE IF NOT EXISTS stage_slot_assignment_item (
            microscope_id TEXT NOT NULL,
            equipment_revision INTEGER NOT NULL,
            stage_component_id TEXT NOT NULL,
            configuration_id TEXT NOT NULL,
            revision INTEGER NOT NULL,
            stage_slot_id TEXT NOT NULL,
            slide_id TEXT NOT NULL,
            slide_revision INTEGER NOT NULL CHECK(slide_revision>0),
            PRIMARY KEY(
                microscope_id,equipment_revision,stage_component_id,
                configuration_id,revision,stage_slot_id
            ),
            UNIQUE(
                microscope_id,equipment_revision,stage_component_id,
                configuration_id,revision,slide_id
            ),
            FOREIGN KEY(
                microscope_id,equipment_revision,stage_component_id,
                configuration_id,revision
            ) REFERENCES stage_slot_assignment_revision(
                microscope_id,equipment_revision,stage_component_id,
                configuration_id,revision
            ),
            FOREIGN KEY(slide_id,slide_revision)
                REFERENCES slide_revision(slide_id,revision)
        );

        CREATE TABLE IF NOT EXISTS stage_slot_assignment_current (
            microscope_id TEXT NOT NULL,
            equipment_revision INTEGER NOT NULL CHECK(equipment_revision>0),
            stage_component_id TEXT NOT NULL,
            configuration_id TEXT NOT NULL,
            revision INTEGER NOT NULL CHECK(revision>0),
            PRIMARY KEY(
                microscope_id,equipment_revision,stage_component_id,configuration_id
            ),
            FOREIGN KEY(
                microscope_id,equipment_revision,stage_component_id,
                configuration_id,revision
            ) REFERENCES stage_slot_assignment_revision(
                microscope_id,equipment_revision,stage_component_id,
                configuration_id,revision
            )
        );

        CREATE INDEX IF NOT EXISTS stage_slot_assignment_slide_lookup
        ON stage_slot_assignment_item(slide_id,slide_revision);

        CREATE TRIGGER IF NOT EXISTS stage_slot_assignment_revision_sequential_insert
        BEFORE INSERT ON stage_slot_assignment_revision
        WHEN NEW.revision != COALESCE((
            SELECT revision FROM stage_slot_assignment_current
            WHERE microscope_id=NEW.microscope_id
              AND equipment_revision=NEW.equipment_revision
              AND stage_component_id=NEW.stage_component_id
              AND configuration_id=NEW.configuration_id
        ),0)+1
        BEGIN SELECT RAISE(ABORT,'StageSlot assignment revisions must be sequential'); END;

        CREATE TRIGGER IF NOT EXISTS stage_slot_assignment_revision_no_update
        BEFORE UPDATE ON stage_slot_assignment_revision
        BEGIN SELECT RAISE(ABORT,'StageSlot assignment revisions are immutable'); END;
        CREATE TRIGGER IF NOT EXISTS stage_slot_assignment_revision_no_delete
        BEFORE DELETE ON stage_slot_assignment_revision
        BEGIN SELECT RAISE(ABORT,'StageSlot assignment revisions are immutable'); END;
        CREATE TRIGGER IF NOT EXISTS stage_slot_assignment_item_no_update
        BEFORE UPDATE ON stage_slot_assignment_item
        BEGIN SELECT RAISE(ABORT,'StageSlot assignment items are immutable'); END;
        CREATE TRIGGER IF NOT EXISTS stage_slot_assignment_item_no_delete
        BEFORE DELETE ON stage_slot_assignment_item
        BEGIN SELECT RAISE(ABORT,'StageSlot assignment items are immutable'); END;

        CREATE TRIGGER IF NOT EXISTS stage_slot_assignment_current_initial
        BEFORE INSERT ON stage_slot_assignment_current
        WHEN NEW.revision!=1
        BEGIN SELECT RAISE(ABORT,'Initial StageSlot assignment revision must be one'); END;
        CREATE TRIGGER IF NOT EXISTS stage_slot_assignment_current_monotonic
        BEFORE UPDATE ON stage_slot_assignment_current
        WHEN NEW.revision!=OLD.revision+1
        BEGIN SELECT RAISE(ABORT,'StageSlot assignment current revision must advance by one'); END;
        CREATE TRIGGER IF NOT EXISTS stage_slot_assignment_current_no_delete
        BEFORE DELETE ON stage_slot_assignment_current
        BEGIN SELECT RAISE(ABORT,'StageSlot assignment current pointer cannot be deleted'); END;
    )SQL";
    if(!execute(database_,schema))
        return core::Status<StageSlotAssignmentsStoreError>::failure(
            makeError(ErrorCode::SchemaMigrationFailed,database_));

    const char* registrationSchema=R"SQL(
        CREATE TABLE IF NOT EXISTS stage_slot_assignment_registration (
            microscope_id TEXT NOT NULL,
            equipment_revision INTEGER NOT NULL,
            stage_component_id TEXT NOT NULL,
            configuration_id TEXT NOT NULL,
            revision INTEGER NOT NULL,
            stage_slot_id TEXT NOT NULL,
            calibration_revision_id TEXT NOT NULL,
            slide_x_extent_micrometers REAL NOT NULL CHECK(slide_x_extent_micrometers>0),
            slide_y_extent_micrometers REAL NOT NULL CHECK(slide_y_extent_micrometers>0),
            accepted_maximum_fit_residual_nm REAL NOT NULL
                CHECK(accepted_maximum_fit_residual_nm>=0),
            PRIMARY KEY(
                microscope_id,equipment_revision,stage_component_id,
                configuration_id,revision,stage_slot_id
            ),
            FOREIGN KEY(
                microscope_id,equipment_revision,stage_component_id,
                configuration_id,revision,stage_slot_id
            ) REFERENCES stage_slot_assignment_item(
                microscope_id,equipment_revision,stage_component_id,
                configuration_id,revision,stage_slot_id
            )
        );

        CREATE TABLE IF NOT EXISTS stage_slot_assignment_registration_corner (
            microscope_id TEXT NOT NULL,
            equipment_revision INTEGER NOT NULL,
            stage_component_id TEXT NOT NULL,
            configuration_id TEXT NOT NULL,
            revision INTEGER NOT NULL,
            stage_slot_id TEXT NOT NULL,
            corner TEXT NOT NULL CHECK(corner IN (
                'top-left','top-right','bottom-right','bottom-left'
            )),
            stage_x_nm REAL NOT NULL,
            stage_y_nm REAL NOT NULL,
            observation_source TEXT NOT NULL CHECK(observation_source IN (
                'hardware-readback','device-event','sdk-status',
                'operator-confirmation','calibrated-estimate','adapter-report'
            )),
            uncertainty_nm REAL NOT NULL CHECK(uncertainty_nm>=0),
            PRIMARY KEY(
                microscope_id,equipment_revision,stage_component_id,
                configuration_id,revision,stage_slot_id,corner
            ),
            FOREIGN KEY(
                microscope_id,equipment_revision,stage_component_id,
                configuration_id,revision,stage_slot_id
            ) REFERENCES stage_slot_assignment_registration(
                microscope_id,equipment_revision,stage_component_id,
                configuration_id,revision,stage_slot_id
            )
        );

        CREATE TRIGGER IF NOT EXISTS stage_slot_assignment_registration_no_update
        BEFORE UPDATE ON stage_slot_assignment_registration
        BEGIN SELECT RAISE(ABORT,'StageSlot registrations are immutable'); END;
        CREATE TRIGGER IF NOT EXISTS stage_slot_assignment_registration_no_delete
        BEFORE DELETE ON stage_slot_assignment_registration
        BEGIN SELECT RAISE(ABORT,'StageSlot registrations are immutable'); END;
        CREATE TRIGGER IF NOT EXISTS stage_slot_assignment_registration_corner_no_update
        BEFORE UPDATE ON stage_slot_assignment_registration_corner
        BEGIN SELECT RAISE(ABORT,'StageSlot corner measurements are immutable'); END;
        CREATE TRIGGER IF NOT EXISTS stage_slot_assignment_registration_corner_no_delete
        BEFORE DELETE ON stage_slot_assignment_registration_corner
        BEGIN SELECT RAISE(ABORT,'StageSlot corner measurements are immutable'); END;
    )SQL";
    if(!execute(database_,registrationSchema))
        return core::Status<StageSlotAssignmentsStoreError>::failure(
            makeError(ErrorCode::SchemaMigrationFailed,database_));

    if(storedSchemaVersion==1) {
        if(!execute(database_,
            "UPDATE stage_slot_assignments_store_metadata SET schema_version=2 "
            "WHERE singleton=1 AND schema_version=1"))
            return core::Status<StageSlotAssignmentsStoreError>::failure(
                makeError(ErrorCode::SchemaMigrationFailed,database_));
    }
    return core::Status<StageSlotAssignmentsStoreError>::success();
}

core::Status<StageSlotAssignmentsStoreError>
StageSlotAssignmentsSqliteStore::saveCurrentState(
    const core::MicroscopeId& microscopeId,
    const core::StageSpecification& installedStage,
    const navigation::StageSlotAssignments& assignments,
    const std::vector<core::Slide>& currentSlides,
    std::uint64_t expectedCurrentRevision) {
    std::lock_guard<std::mutex> lock(mutex_);

    const auto equipmentRevision=assignments.equipmentDefinitionRevision();
    if(installedStage.componentId()!=assignments.stageComponentId())
        return core::Status<StageSlotAssignmentsStoreError>::failure(
            makeError(ErrorCode::EquipmentBindingMismatch,database_,
                "The StageSlot assignment snapshot belongs to a different installed Stage component."));
    const auto* configuration=findConfiguration(installedStage,assignments.configurationId());
    if(configuration==nullptr)
        return core::Status<StageSlotAssignmentsStoreError>::failure(
            makeError(ErrorCode::EquipmentBindingMismatch,database_,
                "The StageSlot assignment configuration is absent from installed equipment."));
    if(equipmentRevision==0||
       equipmentRevision>static_cast<std::uint64_t>(
           std::numeric_limits<sqlite3_int64>::max())||
       expectedCurrentRevision>static_cast<std::uint64_t>(
           std::numeric_limits<sqlite3_int64>::max())||
       expectedCurrentRevision==static_cast<std::uint64_t>(
           std::numeric_limits<sqlite3_int64>::max()))
        return core::Status<StageSlotAssignmentsStoreError>::failure(
            makeError(ErrorCode::InvalidRevision,database_));

    Transaction transaction(database_);
    if(!transaction.active())
        return core::Status<StageSlotAssignmentsStoreError>::failure(
            makeError(ErrorCode::SqliteOperationFailed,database_));

    std::optional<std::uint64_t> currentRevision;
    if(!readCurrentAssignmentRevision(database_,microscopeId,equipmentRevision,
        assignments.stageComponentId(),assignments.configurationId(),currentRevision))
        return core::Status<StageSlotAssignmentsStoreError>::failure(
            makeError(ErrorCode::SqliteOperationFailed,database_));
    const std::uint64_t actual=currentRevision.value_or(0);
    if(actual!=expectedCurrentRevision)
        return core::Status<StageSlotAssignmentsStoreError>::failure(
            makeError(ErrorCode::RevisionConflict,database_,
                "The StageSlot assignment revision changed since it was loaded."));

    for(const auto& assignment:assignments.assignments()) {
        const core::Slide* selected=nullptr;
        for(const auto& slide:currentSlides) {
            if(slide.id()!=assignment.slideId) continue;
            if(selected!=nullptr)
                return core::Status<StageSlotAssignmentsStoreError>::failure(
                    makeError(ErrorCode::InvalidPersistedRecord,database_,
                        "The current Slide list contains a duplicate SlideId."));
            selected=&slide;
        }
        if(selected==nullptr)
            return core::Status<StageSlotAssignmentsStoreError>::failure(
                makeError(ErrorCode::SlideNotPersisted,database_,
                    "Every occupied StageSlot must resolve to a supplied current Slide."));
        if(selected->revision()!=assignment.slideRevision)
            return core::Status<StageSlotAssignmentsStoreError>::failure(
                makeError(ErrorCode::SlideRevisionMismatch,database_,
                    "The supplied Slide revision differs from the assignment's pinned revision."));

        const auto slot=std::find_if(configuration->slots().begin(),configuration->slots().end(),
            [&assignment](const core::StageSlotSpecification& item){
                return item.id()==assignment.stageSlotId;
            });
        if(slot==configuration->slots().end())
            return core::Status<StageSlotAssignmentsStoreError>::failure(
                makeError(ErrorCode::EquipmentBindingMismatch,database_,
                    "The assigned StageSlot is absent from the installed StageConfiguration."));
        const long double availableX=static_cast<long double>(
            slot->xRange().maximum()-slot->xRange().minimum());
        const long double availableY=static_cast<long double>(
            slot->yRange().maximum()-slot->yRange().minimum());
        const long double requiredX=
            static_cast<long double>(selected->geometry().xExtentMicrometers())*1'000.0L;
        const long double requiredY=
            static_cast<long double>(selected->geometry().yExtentMicrometers())*1'000.0L;
        if(!std::isfinite(requiredX)||!std::isfinite(requiredY)||
           requiredX>availableX||requiredY>availableY)
            return core::Status<StageSlotAssignmentsStoreError>::failure(
                makeError(ErrorCode::SlideGeometryMismatch,database_,
                    "The Slide's physical X/Y extents exceed the assigned StageSlot's usable ranges."));

        std::optional<std::uint64_t> persistedCurrent;
        if(!readCurrentSlideRevision(database_,assignment.slideId,persistedCurrent))
            return core::Status<StageSlotAssignmentsStoreError>::failure(
                makeError(ErrorCode::SqliteOperationFailed,database_));
        if(!persistedCurrent)
            return core::Status<StageSlotAssignmentsStoreError>::failure(
                makeError(ErrorCode::SlideNotPersisted,database_,
                    "The assigned Slide has no persisted current revision."));
        if(*persistedCurrent!=assignment.slideRevision)
            return core::Status<StageSlotAssignmentsStoreError>::failure(
                makeError(ErrorCode::SlideRevisionMismatch,database_,
                    "The assigned Slide is not at the current persisted revision."));
    }

    if(actual==static_cast<std::uint64_t>(
        std::numeric_limits<sqlite3_int64>::max()))
        return core::Status<StageSlotAssignmentsStoreError>::failure(
            makeError(ErrorCode::InvalidRevision,database_));
    const auto newRevision=actual+1;

    Statement insertRevision(database_,
        "INSERT INTO stage_slot_assignment_revision("
        "microscope_id,equipment_revision,stage_component_id,configuration_id,revision"
        ") VALUES(?1,?2,?3,?4,?5)");
    if(!insertRevision.prepared()||
       !bindText(insertRevision.get(),1,microscopeId.value())||
       !bindRevision(insertRevision.get(),2,equipmentRevision)||
       !bindText(insertRevision.get(),3,assignments.stageComponentId().value())||
       !bindText(insertRevision.get(),4,assignments.configurationId().value())||
       !bindRevision(insertRevision.get(),5,newRevision)||
       insertRevision.step()!=SQLITE_DONE)
        return core::Status<StageSlotAssignmentsStoreError>::failure(
            makeError(ErrorCode::SqliteOperationFailed,database_));

    Statement insertItem(database_,
        "INSERT INTO stage_slot_assignment_item("
        "microscope_id,equipment_revision,stage_component_id,configuration_id,"
        "revision,stage_slot_id,slide_id,slide_revision"
        ") VALUES(?1,?2,?3,?4,?5,?6,?7,?8)");
    if(!insertItem.prepared())
        return core::Status<StageSlotAssignmentsStoreError>::failure(
            makeError(ErrorCode::SqliteOperationFailed,database_));
    for(const auto& assignment:assignments.assignments()) {
        sqlite3_reset(insertItem.get());
        sqlite3_clear_bindings(insertItem.get());
        if(!bindText(insertItem.get(),1,microscopeId.value())||
           !bindRevision(insertItem.get(),2,equipmentRevision)||
           !bindText(insertItem.get(),3,assignments.stageComponentId().value())||
           !bindText(insertItem.get(),4,assignments.configurationId().value())||
           !bindRevision(insertItem.get(),5,newRevision)||
           !bindText(insertItem.get(),6,assignment.stageSlotId.value())||
           !bindText(insertItem.get(),7,assignment.slideId.value())||
           !bindRevision(insertItem.get(),8,assignment.slideRevision)||
           insertItem.step()!=SQLITE_DONE)
            return core::Status<StageSlotAssignmentsStoreError>::failure(
                makeError(ErrorCode::SqliteOperationFailed,database_));

        if(assignment.registration) {
            const auto& registration=*assignment.registration;
            Statement insertRegistration(database_,
                "INSERT INTO stage_slot_assignment_registration("
                "microscope_id,equipment_revision,stage_component_id,configuration_id,"
                "revision,stage_slot_id,calibration_revision_id,"
                "slide_x_extent_micrometers,slide_y_extent_micrometers,"
                "accepted_maximum_fit_residual_nm"
                ") VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10)");
            const auto& geometry=registration.slideGeometry();
            if(!insertRegistration.prepared()||
               !bindText(insertRegistration.get(),1,microscopeId.value())||
               !bindRevision(insertRegistration.get(),2,equipmentRevision)||
               !bindText(insertRegistration.get(),3,assignments.stageComponentId().value())||
               !bindText(insertRegistration.get(),4,assignments.configurationId().value())||
               !bindRevision(insertRegistration.get(),5,newRevision)||
               !bindText(insertRegistration.get(),6,assignment.stageSlotId.value())||
               !bindText(insertRegistration.get(),7,registration.calibrationRevision().value())||
               !bindReal(insertRegistration.get(),8,geometry.xExtentMicrometers())||
               !bindReal(insertRegistration.get(),9,geometry.yExtentMicrometers())||
               !bindReal(insertRegistration.get(),10,
                   registration.acceptedMaximumFitResidualNanometres())||
               insertRegistration.step()!=SQLITE_DONE)
                return core::Status<StageSlotAssignmentsStoreError>::failure(
                    makeError(ErrorCode::SqliteOperationFailed,database_));

            Statement insertCorner(database_,
                "INSERT INTO stage_slot_assignment_registration_corner("
                "microscope_id,equipment_revision,stage_component_id,configuration_id,"
                "revision,stage_slot_id,corner,stage_x_nm,stage_y_nm,"
                "observation_source,uncertainty_nm"
                ") VALUES(?1,?2,?3,?4,?5,?6,?7,?8,?9,?10,?11)");
            if(!insertCorner.prepared())
                return core::Status<StageSlotAssignmentsStoreError>::failure(
                    makeError(ErrorCode::SqliteOperationFailed,database_));
            for(const auto& measurement:registration.cornerMeasurements()) {
                const char* corner=cornerName(measurement.corner());
                const char* source=observationSourceName(measurement.source());
                if(corner==nullptr||source==nullptr||
                   !bindText(insertCorner.get(),1,microscopeId.value())||
                   !bindRevision(insertCorner.get(),2,equipmentRevision)||
                   !bindText(insertCorner.get(),3,assignments.stageComponentId().value())||
                   !bindText(insertCorner.get(),4,assignments.configurationId().value())||
                   !bindRevision(insertCorner.get(),5,newRevision)||
                   !bindText(insertCorner.get(),6,assignment.stageSlotId.value())||
                   !bindText(insertCorner.get(),7,corner)||
                   !bindReal(insertCorner.get(),8,measurement.stagePosition().x)||
                   !bindReal(insertCorner.get(),9,measurement.stagePosition().y)||
                   !bindText(insertCorner.get(),10,source)||
                   !bindReal(insertCorner.get(),11,measurement.uncertaintyNanometres())||
                   insertCorner.step()!=SQLITE_DONE)
                    return core::Status<StageSlotAssignmentsStoreError>::failure(
                        makeError(ErrorCode::SqliteOperationFailed,database_));
                sqlite3_reset(insertCorner.get());
                sqlite3_clear_bindings(insertCorner.get());
            }
        }
    }

    if(actual==0) {
        Statement insertCurrent(database_,
            "INSERT INTO stage_slot_assignment_current("
            "microscope_id,equipment_revision,stage_component_id,configuration_id,revision"
            ") VALUES(?1,?2,?3,?4,?5)");
        if(!insertCurrent.prepared()||
           !bindText(insertCurrent.get(),1,microscopeId.value())||
           !bindRevision(insertCurrent.get(),2,equipmentRevision)||
           !bindText(insertCurrent.get(),3,assignments.stageComponentId().value())||
           !bindText(insertCurrent.get(),4,assignments.configurationId().value())||
           !bindRevision(insertCurrent.get(),5,newRevision)||
           insertCurrent.step()!=SQLITE_DONE)
            return core::Status<StageSlotAssignmentsStoreError>::failure(
                makeError(ErrorCode::SqliteOperationFailed,database_));
    } else {
        Statement updateCurrent(database_,
            "UPDATE stage_slot_assignment_current SET revision=?1 "
            "WHERE microscope_id=?2 AND equipment_revision=?3 "
            "AND stage_component_id=?4 AND configuration_id=?5 AND revision=?6");
        if(!updateCurrent.prepared()||
           !bindRevision(updateCurrent.get(),1,newRevision)||
           !bindText(updateCurrent.get(),2,microscopeId.value())||
           !bindRevision(updateCurrent.get(),3,equipmentRevision)||
           !bindText(updateCurrent.get(),4,assignments.stageComponentId().value())||
           !bindText(updateCurrent.get(),5,assignments.configurationId().value())||
           !bindRevision(updateCurrent.get(),6,expectedCurrentRevision)||
           updateCurrent.step()!=SQLITE_DONE||sqlite3_changes(database_)!=1)
            return core::Status<StageSlotAssignmentsStoreError>::failure(
                makeError(ErrorCode::RevisionConflict,database_,
                    "The StageSlot assignment current pointer changed concurrently."));
    }

    if(!transaction.commit())
        return core::Status<StageSlotAssignmentsStoreError>::failure(
            makeError(ErrorCode::SqliteOperationFailed,database_));
    return core::Status<StageSlotAssignmentsStoreError>::success();
}

core::Result<std::optional<PersistedStageSlotAssignments>,StageSlotAssignmentsStoreError>
StageSlotAssignmentsSqliteStore::loadCurrentState(
    const core::MicroscopeId& microscopeId,
    std::uint64_t equipmentDefinitionRevision,
    const core::StageSpecification& installedStage,
    const core::StageConfigurationId& configurationId) {
    std::lock_guard<std::mutex> lock(mutex_);
    if(equipmentDefinitionRevision==0||
       equipmentDefinitionRevision>static_cast<std::uint64_t>(
           std::numeric_limits<sqlite3_int64>::max()))
        return core::Result<std::optional<PersistedStageSlotAssignments>,
            StageSlotAssignmentsStoreError>::failure(
                makeError(ErrorCode::InvalidRevision,database_));
    if(findConfiguration(installedStage,configurationId)==nullptr)
        return core::Result<std::optional<PersistedStageSlotAssignments>,
            StageSlotAssignmentsStoreError>::failure(
                makeError(ErrorCode::EquipmentBindingMismatch,database_,
                    "The requested StageConfiguration is absent from the installed Stage equipment."));
    std::optional<std::uint64_t> revision;
    if(!readCurrentAssignmentRevision(database_,microscopeId,equipmentDefinitionRevision,
        installedStage.componentId(),configurationId,revision))
        return core::Result<std::optional<PersistedStageSlotAssignments>,
            StageSlotAssignmentsStoreError>::failure(
                makeError(ErrorCode::SqliteOperationFailed,database_));
    if(!revision)
        return core::Result<std::optional<PersistedStageSlotAssignments>,
            StageSlotAssignmentsStoreError>::success(std::nullopt);
    return loadRevisionUnlocked(microscopeId,equipmentDefinitionRevision,
        installedStage,configurationId,*revision);
}

core::Result<std::optional<PersistedStageSlotAssignments>,StageSlotAssignmentsStoreError>
StageSlotAssignmentsSqliteStore::loadRevision(
    const core::MicroscopeId& microscopeId,
    std::uint64_t equipmentDefinitionRevision,
    const core::StageSpecification& installedStage,
    const core::StageConfigurationId& configurationId,
    std::uint64_t revision) {
    std::lock_guard<std::mutex> lock(mutex_);
    if(equipmentDefinitionRevision==0||revision==0||
       equipmentDefinitionRevision>static_cast<std::uint64_t>(
           std::numeric_limits<sqlite3_int64>::max())||
       revision>static_cast<std::uint64_t>(
           std::numeric_limits<sqlite3_int64>::max()))
        return core::Result<std::optional<PersistedStageSlotAssignments>,
            StageSlotAssignmentsStoreError>::failure(
                makeError(ErrorCode::InvalidRevision,database_));
    return loadRevisionUnlocked(microscopeId,equipmentDefinitionRevision,
        installedStage,configurationId,revision);
}

core::Result<std::optional<PersistedStageSlotAssignments>,StageSlotAssignmentsStoreError>
StageSlotAssignmentsSqliteStore::loadRevisionUnlocked(
    const core::MicroscopeId& microscopeId,
    std::uint64_t equipmentDefinitionRevision,
    const core::StageSpecification& installedStage,
    const core::StageConfigurationId& configurationId,
    std::uint64_t revision) {
    const auto* configuration=findConfiguration(installedStage,configurationId);
    if(configuration==nullptr)
        return core::Result<std::optional<PersistedStageSlotAssignments>,
            StageSlotAssignmentsStoreError>::failure(
                makeError(ErrorCode::EquipmentBindingMismatch,database_,
                    "The requested StageConfiguration is absent from the installed Stage equipment."));

    Statement checkRevision(database_,
        "SELECT 1 FROM stage_slot_assignment_revision "
        "WHERE microscope_id=?1 AND equipment_revision=?2 "
        "AND stage_component_id=?3 AND configuration_id=?4 AND revision=?5");
    if(!checkRevision.prepared()||
       !bindText(checkRevision.get(),1,microscopeId.value())||
       !bindRevision(checkRevision.get(),2,equipmentDefinitionRevision)||
       !bindText(checkRevision.get(),3,installedStage.componentId().value())||
       !bindText(checkRevision.get(),4,configurationId.value())||
       !bindRevision(checkRevision.get(),5,revision))
        return core::Result<std::optional<PersistedStageSlotAssignments>,
            StageSlotAssignmentsStoreError>::failure(
                makeError(ErrorCode::SqliteOperationFailed,database_));
    const int exists=checkRevision.step();
    if(exists==SQLITE_DONE)
        return core::Result<std::optional<PersistedStageSlotAssignments>,
            StageSlotAssignmentsStoreError>::success(std::nullopt);
    if(exists!=SQLITE_ROW||checkRevision.step()!=SQLITE_DONE)
        return core::Result<std::optional<PersistedStageSlotAssignments>,
            StageSlotAssignmentsStoreError>::failure(
                makeError(ErrorCode::SqliteOperationFailed,database_));

    Statement items(database_,
        "SELECT stage_slot_id,slide_id,slide_revision "
        "FROM stage_slot_assignment_item "
        "WHERE microscope_id=?1 AND equipment_revision=?2 "
        "AND stage_component_id=?3 AND configuration_id=?4 AND revision=?5 "
        "ORDER BY stage_slot_id");
    if(!items.prepared()||
       !bindText(items.get(),1,microscopeId.value())||
       !bindRevision(items.get(),2,equipmentDefinitionRevision)||
       !bindText(items.get(),3,installedStage.componentId().value())||
       !bindText(items.get(),4,configurationId.value())||
       !bindRevision(items.get(),5,revision))
        return core::Result<std::optional<PersistedStageSlotAssignments>,
            StageSlotAssignmentsStoreError>::failure(
                makeError(ErrorCode::SqliteOperationFailed,database_));

    std::vector<navigation::StageSlotAssignment> assignments;
    int result=SQLITE_ROW;
    while((result=items.step())==SQLITE_ROW) {
        const auto* slotText=sqlite3_column_text(items.get(),0);
        const auto* slideText=sqlite3_column_text(items.get(),1);
        const int slotBytes=sqlite3_column_bytes(items.get(),0);
        const int slideBytes=sqlite3_column_bytes(items.get(),1);
        std::uint64_t slideRevision=0;
        if(slotText==nullptr||slideText==nullptr||slotBytes<=0||slideBytes<=0||
           !readPositiveRevision(items.get(),2,slideRevision))
            return core::Result<std::optional<PersistedStageSlotAssignments>,
                StageSlotAssignmentsStoreError>::failure(
                    makeError(ErrorCode::InvalidPersistedRecord,database_,
                        "A StageSlot assignment row contains invalid identifiers or revision data."));
        auto slotId=core::StageSlotId::create(std::string(
            reinterpret_cast<const char*>(slotText),static_cast<std::size_t>(slotBytes)));
        auto slideId=core::SlideId::create(std::string(
            reinterpret_cast<const char*>(slideText),static_cast<std::size_t>(slideBytes)));
        if(!slotId||!slideId)
            return core::Result<std::optional<PersistedStageSlotAssignments>,
                StageSlotAssignmentsStoreError>::failure(
                    makeError(ErrorCode::InvalidPersistedRecord,database_,
                        "A StageSlot assignment row contains an invalid strong identifier."));
        auto restoredSlotId=std::move(slotId).value();
        auto restoredSlideId=std::move(slideId).value();
        bool registrationValid=true;
        auto registration=readStageSlotRegistration(database_,microscopeId,
            equipmentDefinitionRevision,installedStage.componentId(),configurationId,
            revision,restoredSlotId,restoredSlideId,slideRevision,registrationValid);
        if(!registrationValid)
            return core::Result<std::optional<PersistedStageSlotAssignments>,
                StageSlotAssignmentsStoreError>::failure(
                    makeError(ErrorCode::InvalidPersistedRecord,database_,
                        "A persisted StageSlot four-corner registration is malformed."));
        assignments.push_back({std::move(restoredSlotId),std::move(restoredSlideId),
            slideRevision,std::move(registration)});
    }
    if(result!=SQLITE_DONE)
        return core::Result<std::optional<PersistedStageSlotAssignments>,
            StageSlotAssignmentsStoreError>::failure(
                makeError(ErrorCode::SqliteOperationFailed,database_));

    auto restored=navigation::StageSlotAssignments::create(
        installedStage.componentId(),equipmentDefinitionRevision,
        *configuration,std::move(assignments));
    if(!restored)
        return core::Result<std::optional<PersistedStageSlotAssignments>,
            StageSlotAssignmentsStoreError>::failure(
                makeError(ErrorCode::InvalidPersistedRecord,database_,
                    "Persisted Slide assignments violate the installed StageSlot model."));
    PersistedStageSlotAssignments value{revision,std::move(restored).value()};
    return core::Result<std::optional<PersistedStageSlotAssignments>,
        StageSlotAssignmentsStoreError>::success(std::move(value));
}

} // namespace microscope::application::persistence
