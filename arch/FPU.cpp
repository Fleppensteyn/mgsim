#include "sim/config.h"
#include "arch/FPU.h"

#include <algorithm>
#include <cctype>
#include <cassert>
#include <cmath>
#include <iomanip>

using namespace std;

namespace Simulator
{

struct FPU::Operation
{
    FPUOperation op;
    int          size;
    double       Rav, Rbv;
    RegAddr      Rc;
    std::string  str() const;
    SERIALIZE(a) { a & op & size & Rav & Rbv & Rc; }
};

class FPU::Source : public Object
{
private:
    Buffer<Operation>        inputs;     ///< Input queue for operations from this source
    StorageTraceSet          outputs;    ///< Set of storage trace each output can generate
    IFPUClient*              client;     ///< Component accepting results for this source
    DefineStateVariable(CycleNo, last_write); ///< Last time an FPU pipe wrote back to this source
    DefineStateVariable(unsigned, last_unit);  ///< Unit that did the last (or current) write

    friend class FPU;
public:
    Source(const std::string& name, Object& parent, Clock& clock);
    Source(const Source&) = delete;
    Source& operator=(const Source&) = delete;
};


static const char* const OperationNames[FPU_NUM_OPS] = {
    "ADD", "SUB", "MUL", "DIV", "SQRT"
};

StorageTraceSet FPU::GetSourceTrace(size_t source) const
{
    return m_sources[source]->inputs;
}

StorageTraceSet FPU::CreateStoragePermutation(size_t num_sources, std::vector<bool>& visited)
{
    StorageTraceSet res;
    for (size_t i = 0; i < num_sources; ++i)
    {
        if (!visited[i])
        {
            visited[i] = true;
            StorageTraceSet perms = CreateStoragePermutation(num_sources, visited);
            visited[i] = false;

            res ^= m_sources[i]->outputs;
            res ^= m_sources[i]->outputs * perms;
        }
    }
    return res;
}

size_t FPU::RegisterSource(IFPUClient& client, const StorageTraceSet& output)
{
    for (size_t i = 0; i < m_sources.size(); ++i)
    {
        if (m_sources[i]->client == NULL)
        {
            m_sources[i]->client = &client;
            m_sources[i]->outputs = output;

            // Any number of outputs can be written in any order.
            size_t num_sources = i + 1;
            vector<bool> visited(num_sources, false);
            StorageTraceSet outputs = CreateStoragePermutation(num_sources, visited);
            p_Pipeline.SetStorageTraces(opt(outputs) * opt(m_active));
            return i;
        }
    }
    UNREACHABLE;
}

string FPU::Operation::str() const
{
    return OperationNames[op] + std::to_string(size * 8)
        + ' ' + std::to_string(Rav)
        + ", " + std::to_string(Rbv)
        + ", " + Rc.str();
}

bool FPU::QueueOperation(size_t source, FPUOperation fop, int size, double Rav, double Rbv, const RegAddr& Rc)
{
    // The size must be a multiple of the arch's native integer size
    assert(source < m_sources.size());
    assert(m_sources[source]->client != NULL);
    assert(size > 0);
    assert(size % sizeof(Integer) == 0);
    assert(Rc.valid());

    Operation op;
    op.op   = fop;
    op.size = size;
    op.Rav  = Rav;
    op.Rbv  = Rbv;
    op.Rc   = Rc;

    DebugFPUWrite("queuing %s %s",
                  m_sources[source]->client->GetName().c_str(),
                  op.str().c_str());

    if (!m_sources[source]->inputs.Push(std::move(op)))
    {
        return false;
    }

    return true;
}

FPU::Result FPU::CalculateResult(const Operation& op) const
{
    double value;
    switch (op.op)
    {
    case FPU_OP_SQRT: value = sqrt( op.Rbv ); break;
    case FPU_OP_ADD:  value = op.Rav + op.Rbv; break;
    case FPU_OP_SUB:  value = op.Rav - op.Rbv; break;
    case FPU_OP_MUL:  value = op.Rav * op.Rbv; break;
    case FPU_OP_DIV:  value = op.Rav / op.Rbv; break;
    default:          UNREACHABLE; break;
    }

    Result  res;
    res.address = op.Rc;
    res.size    = op.size;
    res.index   = 0;
    res.state   = 1;
    res.value.fromfloat(value, op.size);

    return res;
}

bool FPU::OnCompletion(unsigned int unit, const Result& res) const
{
    const CycleNo now = GetKernel()->GetActiveClock()->GetCycleNo();

    Source *source = m_sources[res.source];

    if (source->m_last_write == now && source->m_last_unit != unit)
    {
        DeadlockWrite("Unable to write back result because another FPU pipe already wrote back this cycle");
        return false;
    }
    source->m_last_write = now;
    source->m_last_unit  = unit;

    // Calculate the address of this register
    RegAddr addr = res.address;
    addr.index += res.index;

    if (!source->client->CheckFPUOutputAvailability(addr))
    {
        DeadlockWrite("Client not ready to accept result");
        return false;
    }

    // Write new value
    unsigned int index = res.index;
#ifdef ARCH_BIG_ENDIAN
    const unsigned int size = res.size / sizeof(Integer);
    index = size - 1 - index;
#endif

    RegValue value;
    value.m_state         = RST_FULL;
    value.m_float.integer = (Integer)(res.value.toint(res.size) >> (sizeof(Integer) * 8 * index));

    if (!source->client->WriteFPUResult(addr, value))
    {
        DeadlockWrite("Unable to write result to %s", addr.str().c_str());
        return false;
    }

    DebugFPUWrite("unit %u completed %s %s <- %s",
                  (unsigned)unit,
                  source->client->GetName().c_str(),
                  addr.str().c_str(),
                  value.str(addr.type).c_str());
    return true;
}

Result FPU::DoPipeline()
{
    size_t num_units_active = 0, num_units_failed = 0;
    size_t num_units_full = 0;
    for (size_t i = 0; i < m_units.size(); ++i)
    {
        // Advance a pipeline
        Unit& unit = m_units[i];
        if (!unit.slots.empty())
        {
            num_units_active++;
            num_units_full++;

            bool advance = true;
            Result&  res = unit.slots.front();
            if (res.state == unit.latency)
            {
                // This operation has completed
                // Write back result
                if (!OnCompletion(i, res))
                {
                    // Stall; stop processing this pipeline
                    num_units_failed++;
                    continue;
                }

                if (res.index + 1 == res.size / sizeof(Integer))
                {
                    // We've written the last register of the result;
                    // clear the result
                    if (unit.slots.size() == 1)
                    {
                        // It's empty now
                        num_units_full--;
                    }
                    COMMIT{ unit.slots.pop_front(); }
                }
                else
                {
                    // We're not done yet -- delay the FPU pipeline
                    advance = false;
                    COMMIT{ ++res.index; }
                }
            }

            if (advance)
            {
                COMMIT
                {
                    // Advance the pipeline
                    for (auto& p : unit.slots)
                        p.state++;
                }
            }
        }
    }

    size_t num_sources_failed = 0, num_sources_active = 0;
    for (size_t i = m_last_source; i < m_last_source + m_sources.size(); ++i)
    {
        size_t source_id = i % m_sources.size();

        // Process an input queue
        Buffer<Operation>& input = m_sources[source_id]->inputs;
        if (!input.Empty())
        {
            num_sources_active++;

            const Operation& op = input.Front();

            // We use a fixed (with modulo) mapping from inputs to units
            const size_t unit_index = m_mapping[op.op][ source_id % m_mapping[op.op].size() ];
            Unit& unit = m_units[ unit_index ];

            if (!IsAcquiring())
            {
                // See if the unit can accept a new request
                // Do this check after the Acquire phase, when the actual pipeline has
                // moved on and made room for our request.
                if (!unit.slots.empty() && (!unit.pipelined || unit.slots.back().state == 1))
                {
                    // The unit is busy or cannot accept a new operation
                    num_sources_failed++;
                    continue;
                }
            }

            // Calculate the result and store it in the unit
            COMMIT{
                Result res = CalculateResult(op);
                res.source = source_id;
                unit.slots.push_back(res);
            }
            num_units_full++;

            DebugFPUWrite("unit %u executing %s %s",
                          (unsigned)unit_index,
                          m_sources[source_id]->client->GetName().c_str(),
                          op.str().c_str());

            // Remove the queued operation from the queue
            input.Pop();
        }
    }

    COMMIT { m_last_source = (m_last_source + 1) % m_sources.size(); }

    if (num_units_full > 0) {
        m_active.Write(true);
    } else {
        m_active.Clear();
    }

    return (num_units_failed == num_units_active && num_sources_failed == num_sources_active) ? FAILED : SUCCESS;
}

FPU::Source::Source(const std::string& name, Object& parent, Clock& clock)
    : Object(name, parent),
      InitBuffer(inputs, clock, "InputQueueSize"),
      outputs(),
      client(NULL),
      InitStateVariable(last_write, 0),
      InitStateVariable(last_unit, 0)
{}


FPU::FPU(const std::string& name, Object& parent, Clock& clock, size_t num_inputs)
    : Object(name, parent),
      InitStorage(m_active, clock),
      m_sources(),
      m_units(),
      m_last_source(0),
      InitProcess(p_Pipeline, DoPipeline)
{
    m_active.Sensitive(p_Pipeline);
    try
    {
        static const char* const Names[FPU_NUM_OPS] = {
            "ADD","SUB","MUL","DIV","SQRT"
        };

        // Construct the FP units
        size_t nUnits = GetConf("NumUnits", size_t);
        if (nUnits == 0)
        {
            throw InvalidArgumentException(*this, "NumUnits not set or zero");
        }
        for (size_t i = 0; i < nUnits; ++i)
        {
            auto uname = "Unit" + std::to_string(i);

            set<FPUOperation> ops;

            // Get ops for this unit
            auto strops = GetConfStrings(uname + "Ops");
            for (auto& p : strops)
            {
                transform(p.begin(), p.end(), p.begin(), ::toupper);
                for (int j = 0; j < FPU_NUM_OPS; ++j) {
                    if (p.compare(Names[j]) == 0) {
                        ops.insert( (FPUOperation)j );
                        break;
                    }
                }
            }
            if (ops.empty())
            {
                throw exceptf<InvalidArgumentException>(*this, "No operation specified for unit %zu", i);
            }

            // Add this unit into the mapping table for the ops it implements
            for (auto& p : ops)
            {
                m_mapping[p].push_back(m_units.size());
            }

            Unit unit;
            unit.latency   = GetConf(uname + "Latency", CycleNo);
            unit.pipelined = GetConf(uname + "Pipelined", bool);
            m_units.push_back(unit);
        }
        for (auto &unit : m_units)
            RegisterStateObject(unit, "unit" + to_string(&unit - &m_units[0]));

        // Construct the sources
        for (size_t i = 0; i < num_inputs; ++i)
        {
            auto sname = "source" + std::to_string(i);

            m_sources.push_back(NULL);
            Source* source = new Source(sname, *this, clock);
            source->inputs.Sensitive(p_Pipeline);
            m_sources.back() = source;
        }
    }
    catch (...)
    {
        Cleanup();
        throw;
    }
}

void FPU::Cleanup()
{
    for (auto s : m_sources)
        delete s;
}

FPU::~FPU()
{
    Cleanup();
}

void FPU::Cmd_Info(std::ostream& out, const std::vector<std::string>& /*arguments*/) const
{
    out <<
    "The Floating-Point Unit executes floating-point operations asynchronously and\n"
    "can be shared among multiple processors. Results are written back asynchronously\n"
    "to the original processor's register file.\n\n"
    "Supported operations:\n"
    "- inspect <component>\n"
    "  Reads and displays the FPU's queues and pipelines.\n";
}

void FPU::Cmd_Read(std::ostream& out, const std::vector<std::string>& /*arguments*/) const
{
    out << fixed << setfill(' ');

    // Print the source queues
    for (auto source : m_sources)
    {
        // Print the source name
        out << "Source: ";
        if (source->client != NULL)
            out << source->client->GetName();
        else
            out << "not connected";
        out << endl;

        if (source->inputs.begin() != source->inputs.end())
        {
            // Print the queued operations
            out << " Op  | Sz |           A          |            B         | Dest " << endl;
            out << "-----+----+----------------------+----------------------+------" << endl;
            for (auto& p : source->inputs)
            {
                out << setw(4) << left << OperationNames[p.op] << right << " | "
                    << setw(2) << left << p.size * 8 << right << " | "
                    << setw(20) << setprecision(12) << p.Rav << " | "
                    << setw(20);
                if (p.op != FPU_OP_SQRT) {
                    out << setprecision(12) << fixed << p.Rbv;
                } else {
                    out << " ";
                }
                out << " | "
                    << p.Rc.str()
                    << endl;
            }
        }
        else
        {
            out << "(Empty)" << endl;
        }
        out << endl;
    }
    out << endl;

    // Print the execution units
    size_t i = 0;
    for (auto& unit : m_units)
    {
        // Print information of this unit
        out << "Unit:       #" << dec << i++ << endl;
        out << "Pipelined:  " << boolalpha << unit.pipelined << endl;
        out << "Latency:    " << unit.latency << " cycles" << endl;
        out << "Operations:";
        for (unsigned int j = 0; j < FPU_NUM_OPS; ++j)
        {
            if (find(m_mapping[j].begin(), m_mapping[j].end(), i) != m_mapping[j].end()) {
                out << " " << OperationNames[j];
            }
        }
        out << endl << endl;

        // Print pipeline
        if (unit.slots.empty())
        {
            out << "(Empty)" << endl;
        }
        else
        {
            out << " t | Sz |        Result       |  Reg  | Destination" << endl;
            out << "---+----+---------------------+-------+--------------------" << endl;
            for (auto& p : unit.slots)
            {
                out << setw(2) << p.state << " | "
                    << setw(2) << p.size * 8 << " | "
                    << setw(20) << setprecision(12) << p.value.tofloat(p.size)  << " | "
                    << p.address.str() << " | ";
                if (p.source < m_sources.size() && m_sources[p.source]->client)
                    out << m_sources[p.source]->client->GetName();
                else
                    out << "<invalid source>";
                out << endl;
            }
            out << endl;
        }
        out << endl;
    }
}

FPU::IFPUClient::~IFPUClient()
{}

}
