#ifndef _BLESS_HPP_
#define _BLESS_HPP_

#include <string>
#include <queue>
#include <vector>

#include "module.hpp"
#include "router.hpp"
#include "routefunc.hpp"

class Bless : public Router
{
        //	Each stage buffer is maintained as a map of <arrival_time:Flit*>
        //	but the length should not exceed 2 flit widths
        //	This is necessary to allow for code to depict
        //	segments working in parallel
        vector<map<int, Flit *> > _input_buffer;
        vector<map<int, Flit *> > _output_buffer;
        vector<map<int, Flit *> > _stage_1;
        //	No need for extra flags showing occupancy as each buffer is timed

        queue<Flit *> _inject_queue;

        int router_type;
        int _time;
        int _inject_slot;
        int last_channel;

        cRoutingFunction _rf;

        virtual void _InternalStep();
        void _IncomingFlits();
        void _SendFlits();
        void _EjectFlits();
        void _input_to_stage1();
        void _routing();
        void _stage1_to_output();
        void CheckSanity();

public:
        Bless(const Configuration &config,
              Module *parent, const string &name, int id,
              int inputs, int outputs);
        virtual ~Bless();

        virtual void AddInputChannel(FlitChannel *channel, CreditChannel *ignored);
        virtual void AddOutputChannel(FlitChannel *channel, CreditChannel *ignored);

        int GetInjectStatus();
        void QueueFlit(Flit *f);
        virtual void ReadInputs();
        virtual void WriteOutputs();

        virtual int GetUsedCredit(int o) const { return 0; }
        virtual int GetBufferOccupancy(int i) const { return 0; }

        virtual vector<int> UsedCredits() const { return vector<int>(); }
        virtual vector<int> FreeCredits() const { return vector<int>(); }
        virtual vector<int> MaxCredits() const { return vector<int>(); }

        virtual void Display(ostream &os = cout) const;
};

#endif
